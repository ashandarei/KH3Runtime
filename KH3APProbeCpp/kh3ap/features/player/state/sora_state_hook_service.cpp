#include "kh3ap/app/config.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <polyhook2/Detour/x64Detour.hpp>

#include "kh3ap/features/player/state/sora_state_hook_service.h"
#include "kh3ap/bridge/checks/level_checks.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/features/grants/bad_status_helpers.h"
#include "kh3ap/features/player/characters/character_exploration.h"
#include "kh3ap/features/player/movement/character_rails.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/player/state/sora_level_monitor_state.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/features/player/state/sora_state_revalidation.h"
#include "kh3ap/features/player/state/sora_state_sidecar.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <utility>

#define NOMINMAX
#include <windows.h>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto max_observed_sora_level = 100;
        constexpr auto minimum_sora_owner_address = 0x10000;

        using ::kh3ap::settings::s_bridge_protocol_name;
        using ::kh3ap::settings::s_bridge_protocol_version;
        using ::kh3ap::settings::s_build_stamp;
        using ::kh3ap::settings::s_enable_current_state_write_polyhook;
        using ::kh3ap::settings::s_enable_current_state_write_polyhook_event_log;
        using ::kh3ap::settings::s_enable_runtime_sora_state_event_log;
        using ::kh3ap::settings::s_enable_runtime_sora_state_sidecar;
        using ::kh3ap::settings::s_enable_vanilla_level_write_polyhook;
        using ::kh3ap::settings::s_runtime_sora_state_sidecar_dirty_interval;
        using ::kh3ap::settings::s_runtime_sora_state_sidecar_fallback_interval;

        auto try_read_process_u8(uintptr_t address) -> std::optional<uint8_t>
        {
            if (address == 0)
            {
                return std::nullopt;
            }
            uint8_t value = 0;
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(
                        GetCurrentProcess(),
                        reinterpret_cast<const void*>(address),
                        &value,
                        sizeof(value),
                        &bytes_read) ||
                bytes_read != sizeof(value))
            {
                return std::nullopt;
            }
            return value;
        }

        auto try_read_process_u64(uintptr_t address) -> std::optional<uint64_t>
        {
            if (address == 0)
            {
                return std::nullopt;
            }
            uint64_t value = 0;
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(
                        GetCurrentProcess(),
                        reinterpret_cast<const void*>(address),
                        &value,
                        sizeof(value),
                        &bytes_read) ||
                bytes_read != sizeof(value))
            {
                return std::nullopt;
            }
            return value;
        }

        auto plausible_runtime_sora_level(uint64_t value) -> bool
        {
            return value >= 1 && value <= max_observed_sora_level;
        }
    }

    struct RuntimeSoraStateHookService::Impl
    {
        explicit Impl(RuntimeSoraStateHookConfig value)
            : config(std::move(value))
        {
        }

        auto runtime_context() const -> std::wstring
        {
            return config.runtime_context_resolver
                    ? config.runtime_context_resolver()
                    : std::wstring{};
        }

        auto bridge_dir() const -> std::optional<std::filesystem::path>
        {
            return config.bridge_dir_resolver
                    ? config.bridge_dir_resolver()
                    : std::nullopt;
        }

        auto cached_bridge_dir() -> std::optional<std::filesystem::path>
        {
            if (!cached_bridge_dir_value.has_value())
            {
                cached_bridge_dir_value = bridge_dir();
            }
            return cached_bridge_dir_value;
        }

        auto trusted_current_playable_cached_sora_pawn() const -> UObject*
        {
            auto& pawn_state = config.runtime_state.grant_state().pawn_state();
            UObject* const cached_pawn = pawn_state.object();
            if (!cached_pawn || !pawn_state.candidate_trusted())
            {
                return nullptr;
            }
            return cached_pawn;
        }

        auto try_refresh_sora_pawn_from_level_write_owner(
                UObject* level_owner,
                std::wstring* reason) -> bool
        {
            const auto reject = [reason](std::wstring_view value) {
                if (reason)
                {
                    *reason = std::wstring{value};
                }
                return false;
            };

            if (!is_uobject_valid_fast(level_owner))
            {
                return reject(STR("level_owner_missing"));
            }

            const uintptr_t owner_address = reinterpret_cast<uintptr_t>(level_owner);
            if (owner_address < minimum_sora_owner_address || (owner_address % alignof(void*)) != 0 ||
                !is_uobject_valid_fast(level_owner))
            {
                return reject(STR("level_owner_pointer_invalid"));
            }

            UClass* const owner_class = level_owner->GetClassPrivate();
            const std::wstring class_name = is_uobject_valid_fast(owner_class)
                    ? owner_class->GetName()
                    : std::wstring{};
            if (!config.validated_sora_pawn_name ||
                !config.validated_sora_pawn_name({}, class_name))
            {
                return reject(STR("level_owner_class_not_sora"));
            }

            const std::wstring pawn_name = level_owner->GetFullName();
            if (!config.validated_sora_pawn_name(pawn_name, class_name))
            {
                if (reason)
                {
                    *reason = std::format(
                            STR("level_owner_not_sora_candidate owner={}"),
                            pawn_name);
                }
                return false;
            }

            if (!config.cache_validated_sora_pawn ||
                !config.cache_validated_sora_pawn(
                        level_owner,
                        pawn_name,
                        STR("VanillaLevelWritePolyhook:level_owner")))
            {
                return reject(STR("level_owner_cache_refresh_failed"));
            }
            if (config.clear_special_battle_from_level_owner)
            {
                config.clear_special_battle_from_level_owner(level_owner);
            }

            if (reason)
            {
                *reason = std::format(
                        STR("level_owner_refreshed_sora_pawn owner={}"),
                        pawn_name);
            }
            return true;
        }

        auto should_dispatch_vanilla_level_write_as_sora(
                UObject* level_owner,
                std::wstring* reason) const -> bool
        {
            const auto reject = [reason](std::wstring_view value) {
                if (reason)
                {
                    *reason = std::wstring{value};
                }
                return false;
            };

            if (!is_uobject_valid_fast(level_owner))
            {
                return reject(STR("level_owner_missing"));
            }

            UObject* const cached_pawn =
                    config.runtime_state.grant_state().pawn_state().object();
            if (cached_pawn && level_owner == cached_pawn)
            {
                if (reason)
                {
                    *reason = STR("level_owner_matches_cached_sora_pawn");
                }
                return true;
            }

            if (reason)
            {
                *reason = std::format(
                        STR("level_owner_not_cached_sora_pawn owner=0x{:x} cached_pawn=0x{:x}"),
                        reinterpret_cast<uintptr_t>(level_owner),
                        reinterpret_cast<uintptr_t>(cached_pawn));
            }
            return false;
        }

        auto dispatch_sora_level_change(uint64_t level, UObject* owner) -> void
        {
            if (config.monitor.last_dispatched_runtime_sora_level == level) return;
            config.battle_level_sync.sync_sora_level(
                    level, STR("sora_level_changed"), owner, false, true);
            config.monitor.last_dispatched_runtime_sora_level = level;
        }

        auto flush_vanilla_level_events() -> void
        {
            if constexpr (!s_enable_vanilla_level_write_polyhook)
            {
                return;
            }

            const auto batch = config.memory_hooks.drain_vanilla_level_write_events();
            const auto& events = batch.events;
            const size_t event_count = batch.event_count;
            if (event_count == 0 && batch.dropped_count == 0)
            {
                return;
            }

            UObject* trusted_cached_pawn = trusted_current_playable_cached_sora_pawn();
            for (size_t index = 0; index < event_count; ++index)
            {
                const auto& event = events[index];
                UObject* const level_owner = reinterpret_cast<UObject*>(event.level_owner);
                bool dispatched_as_sora = false;
                bool observed_as_sora = false;
                if (event.old_level == 0)
                {
                    UObject* const cached_pawn =
                            config.runtime_state.grant_state().pawn_state().object();
                    const bool cached_pawn_matches_owner =
                            cached_pawn && level_owner && cached_pawn == level_owner;
                    if (cached_pawn_matches_owner)
                    {
                        observed_as_sora = true;
                    }
                    else if (cached_pawn && trusted_cached_pawn == cached_pawn)
                    {
                        observed_as_sora = false;
                    }
                    else
                    {
                        const bool owner_refreshed =
                                try_refresh_sora_pawn_from_level_write_owner(level_owner, nullptr);
                        observed_as_sora = owner_refreshed;
                        if (owner_refreshed)
                        {
                            trusted_cached_pawn =
                                    config.runtime_state.grant_state().pawn_state().object();
                        }
                    }
                }
                else
                {
                    dispatched_as_sora =
                            should_dispatch_vanilla_level_write_as_sora(level_owner, nullptr);
                    observed_as_sora = dispatched_as_sora;
                }
                if (observed_as_sora && is_uobject_valid_fast(level_owner) &&
                    plausible_runtime_sora_level(event.new_level))
                {
                    config.monitor.last_observed_runtime_sora_level = event.new_level;
                    // Initial loads and real level writes both carry Sora's
                    // authoritative level. Only values cross into the worker.
                    if (const auto directory = bridge_dir())
                        level_checks::observe(*directory, event.new_level);
                    if (event.old_level == 0)
                    {
                        config.battle_level_sync.schedule_initial(event.new_level, level_owner);
                    }
                }
                if (dispatched_as_sora)
                {
                    dispatch_sora_level_change(event.new_level, level_owner);
                }
            }
        }

        auto flush_current_state_events() -> void
        {
            if constexpr (!s_enable_current_state_write_polyhook)
            {
                return;
            }

            const auto batch = config.memory_hooks.drain_current_state_write_events();
            const auto& events = batch.events;
            const size_t event_count = batch.event_count;
            const uint64_t dropped_count = batch.dropped_count;
            if (event_count == 0 && dropped_count == 0)
            {
                return;
            }

            for (size_t index = 0; index < event_count; ++index)
            {
                const auto& event = events[index];
                character_swap::observe_game_over_state_write(event.state_comp,
                        event.replaced && event.replacement_state ? event.replacement_state : event.new_state);
                character_rails::observe_state_write(event.state_comp,
                        event.replaced && event.replacement_state ? event.replacement_state : event.new_state);
                handle_alternate_bad_status_state_write(event.state_comp,
                        event.replaced && event.replacement_state ? event.replacement_state : event.new_state,
                        config.runtime_state);
                if (config.observe_shop_session)
                {
                    const auto shop = character_exploration::observe_shop_state_write(event.state_comp,
                            event.replaced && event.replacement_state ? event.replacement_state : event.new_state);
                    if (shop.has_value())
                    {
                        config.observe_shop_session(*shop);
                    }
                }
            }

            const auto event_bridge_dir = [&]() -> std::optional<std::filesystem::path> {
                if constexpr (!s_enable_current_state_write_polyhook_event_log)
                {
                    return std::nullopt;
                }
                return cached_bridge_dir();
            }();
            if constexpr (s_enable_current_state_write_polyhook_event_log)
            {
                if (event_bridge_dir.has_value() && dropped_count > 0)
                {
                    std::ostringstream json;
                    json << "{\"schema\":\"kh3ap.current_state_write_polyhook_event.v1\"";
                    json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
                    json << ",\"build_stamp\":\"" << json_escape(s_build_stamp) << "\"";
                    json << ",\"event\":\"dropped_events\"";
                    json << ",\"dropped_count\":" << dropped_count;
                    json << "}\n";
                    append_text_file(
                            *event_bridge_dir / channels::current_state_write_polyhook_events,
                            json.str());
                }
            }

            UObject* const cached_pawn =
                    config.runtime_state.grant_state().pawn_state().object();
            if (!cached_pawn)
            {
                return;
            }

            constexpr uintptr_t current_state_id_offset = 0x58;
            const uintptr_t cached_pawn_address = reinterpret_cast<uintptr_t>(cached_pawn);
            bool cached_state_comp_for_pawn =
                    config.monitor.cached_runtime_sora_state_comp_valid &&
                    config.monitor.cached_runtime_sora_pawn_address == cached_pawn_address &&
                    config.monitor.cached_runtime_sora_state_comp_address != 0;
            uintptr_t cached_state_comp_address =
                    config.monitor.cached_runtime_sora_state_comp_address;
            const auto invalidate_cached_current_state_for_event =
                    [&](const RuntimeSoraLevelMonitorState::CurrentStateWritePolyhookEvent& event,
                        UObject* state_comp) {
                        const bool should_log_event =
                                config.monitor.cached_runtime_sora_current_state_valid ||
                                event.state_comp != reinterpret_cast<uintptr_t>(
                                        config.monitor.cached_runtime_sora_state_comp) ||
                                event.new_state != reinterpret_cast<uintptr_t>(
                                        config.monitor.cached_runtime_sora_current_state) ||
                                config.monitor.cached_runtime_sora_current_state_id.has_value() ||
                                !config.monitor.cached_runtime_sora_state_class.empty();

                        config.monitor.cached_runtime_sora_state_comp = state_comp;
                        config.monitor.cached_runtime_sora_current_state =
                                event.new_state == 0
                                        ? nullptr
                                        : reinterpret_cast<UObject*>(event.new_state);
                        config.monitor.cached_runtime_sora_state_comp_valid = true;
                        config.monitor.cached_runtime_sora_current_state_valid = false;
                        config.monitor.cached_runtime_sora_current_state_id = std::nullopt;
                        config.monitor.cached_runtime_sora_state_class.clear();
                        config.monitor.cached_runtime_sora_state_source =
                                STR("current_state_write_polyhook");
                        config.monitor.cached_runtime_sora_state_event_index = event.event_index;
                        config.monitor.cached_runtime_sora_pawn_address = cached_pawn_address;
                        config.monitor.cached_runtime_sora_state_comp_address = event.state_comp;
                        config.monitor.cached_runtime_sora_current_state_address = event.new_state;
                        cached_state_comp_for_pawn = true;
                        cached_state_comp_address = event.state_comp;
                        if (should_log_event)
                        {
                            sidecar_update_pending = true;
                        }
                        if (config.observe_shop_session)
                        {
                            config.observe_shop_session(false);
                        }
                    };

            bool observed_sora_state = false;
            for (size_t index = 0; index < event_count; ++index)
            {
                const auto& event = events[index];
                if (event.state_comp == 0)
                {
                    continue;
                }

                auto* const state_comp = reinterpret_cast<UObject*>(event.state_comp);
                bool state_comp_real = true;
                bool state_comp_matches_cached_pawn = false;
                if (cached_state_comp_for_pawn)
                {
                    if (event.state_comp != cached_state_comp_address)
                    {
                        continue;
                    }
                    state_comp_matches_cached_pawn = true;
                }
                else
                {
                    // A current-state write gives us a live component address.
                    // Validate its raw Outer pointer instead of calling IsReal,
                    // which walks UE4SS's global object array for every rejected
                    // component during multi-character fights.
                    const auto state_comp_outer = try_read_process_u64(
                            event.state_comp +
                            runtime_sora_state_comp_outer_offset);
                    state_comp_real = state_comp_outer.has_value();
                    state_comp_matches_cached_pawn =
                            state_comp_outer.value_or(0) == cached_pawn_address;
                    if (!state_comp_matches_cached_pawn)
                    {
                        continue;
                    }
                }

                if (event.new_state == 0)
                {
                    if (config.observe_cinematic_transition &&
                        runtime_sora_state_matches_kind(
                                RuntimeSoraStateKind::Cinematic,
                                config.monitor.cached_runtime_sora_current_state_id,
                                config.monitor.cached_runtime_sora_state_class))
                    {
                        config.observe_cinematic_transition(false);
                    }
                    invalidate_cached_current_state_for_event(event, state_comp);
                    continue;
                }

                const uintptr_t observed_state_address = event.new_state;
                const uintptr_t effective_state_address =
                        event.replaced && event.replacement_state != 0
                                ? event.replacement_state
                                : event.new_state;
                auto* const observed_state = reinterpret_cast<UObject*>(observed_state_address);
                auto* const current_state = reinterpret_cast<UObject*>(effective_state_address);
                const std::optional<uint8_t> observed_state_id =
                        try_read_process_u8(observed_state_address + current_state_id_offset);
                const std::optional<uint8_t> current_state_id =
                        try_read_process_u8(effective_state_address + current_state_id_offset);
                const bool current_state_real = current_state_id.has_value();
                if (!current_state_id.has_value())
                {
                    invalidate_cached_current_state_for_event(event, state_comp);
                    continue;
                }

                // One ownership observation per matching event batch. Idle
                // update calls without native state events do no lookup work.
                if (!observed_sora_state && config.observe_sora_state)
                {
                    config.observe_sora_state(cached_pawn);
                    observed_sora_state = true;
                }

                const RuntimeSoraStateCatalogEntry* const observed_state_catalog_entry =
                        observed_state_id.has_value()
                                ? lookup_runtime_sora_state_by_id(*observed_state_id)
                                : nullptr;
                const std::wstring observed_state_class = observed_state_catalog_entry
                        ? std::wstring{observed_state_catalog_entry->class_name}
                        : std::wstring{};
                const RuntimeSoraStateCatalogEntry* const state_catalog_entry =
                        lookup_runtime_sora_state_by_id(*current_state_id);
                const std::wstring state_class = state_catalog_entry
                        ? std::wstring{state_catalog_entry->class_name}
                        : std::wstring{};
                const bool was_cinematic = runtime_sora_state_matches_kind(
                        RuntimeSoraStateKind::Cinematic,
                        config.monitor.cached_runtime_sora_current_state_id,
                        config.monitor.cached_runtime_sora_state_class);
                const bool is_cinematic = runtime_sora_state_matches_kind(
                        RuntimeSoraStateKind::Cinematic,
                        current_state_id,
                        state_class);

                const bool should_log_event =
                        event.state_comp != reinterpret_cast<uintptr_t>(
                                config.monitor.cached_runtime_sora_state_comp) ||
                        effective_state_address != reinterpret_cast<uintptr_t>(
                                config.monitor.cached_runtime_sora_current_state) ||
                        current_state_id != config.monitor.cached_runtime_sora_current_state_id ||
                        state_class != config.monitor.cached_runtime_sora_state_class ||
                        event.replaced;

                config.monitor.cached_runtime_sora_state_comp = state_comp;
                config.monitor.cached_runtime_sora_current_state = current_state;
                config.monitor.cached_runtime_sora_state_comp_valid = true;
                config.monitor.cached_runtime_sora_current_state_valid = true;
                config.monitor.cached_runtime_sora_current_state_id = current_state_id;
                config.monitor.cached_runtime_sora_state_class = state_class;
                config.monitor.cached_runtime_sora_state_source =
                        STR("current_state_write_polyhook");
                config.monitor.cached_runtime_sora_state_event_index = event.event_index;
                config.monitor.cached_runtime_sora_pawn_address = cached_pawn_address;
                config.monitor.cached_runtime_sora_state_comp_address = event.state_comp;
                config.monitor.cached_runtime_sora_current_state_address = effective_state_address;
                if (config.observe_cinematic_transition &&
                    was_cinematic != is_cinematic)
                {
                    config.observe_cinematic_transition(is_cinematic);
                }
                if (runtime_sora_state_matches_kind(
                            RuntimeSoraStateKind::Idle,
                            current_state_id,
                            state_class))
                {
                    std::lock_guard<std::mutex> lock{
                            config.monitor.current_state_write_polyhook_event_mutex};
                    config.monitor.cached_runtime_sora_idle_state = current_state;
                    config.monitor.cached_runtime_sora_idle_state_valid = true;
                    config.monitor.cached_runtime_sora_idle_state_address =
                            effective_state_address;
                }
                if (should_log_event)
                {
                    request_music_update(MusicUpdate::Form);
                    sidecar_update_pending = true;
                }
                cached_state_comp_for_pawn = true;
                cached_state_comp_address = event.state_comp;

                if (runtime_sora_state_matches_kind(
                            RuntimeSoraStateKind::WallRun,
                            observed_state_id,
                            observed_state_class))
                {
                    std::lock_guard<std::mutex> lock{
                            config.monitor.current_state_write_polyhook_event_mutex};
                    config.monitor.cached_runtime_sora_wallrun_state = observed_state;
                    config.monitor.cached_runtime_sora_wallrun_state_valid = true;
                    config.monitor.cached_runtime_sora_wallrun_state_address =
                            observed_state_address;
                }
                handle_direct_player_bad_status_state_transition(
                        cached_pawn,
                        state_comp,
                        current_state,
                        static_cast<int32_t>(*current_state_id),
                        config.runtime_state);
                if (config.activate_blocking_ui)
                {
                    config.activate_blocking_ui(state_class, current_state_id);
                }
                if (config.observe_shop_session)
                {
                    config.observe_shop_session(runtime_sora_state_matches_kind(
                            RuntimeSoraStateKind::ShopMenu,
                            current_state_id,
                            state_class));
                }
                if (config.observe_weapon_change &&
                    runtime_sora_state_matches_kind(
                            RuntimeSoraStateKind::WeaponChange,
                            current_state_id,
                            state_class))
                {
                    config.observe_weapon_change();
                }

                if constexpr (s_enable_current_state_write_polyhook_event_log)
                {
                    if (event_bridge_dir.has_value() && should_log_event)
                    {
                        std::ostringstream json;
                        json << "{\"schema\":\"kh3ap.current_state_write_polyhook_event.v1\"";
                        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
                        json << ",\"build_stamp\":\"" << json_escape(s_build_stamp) << "\"";
                        json << ",\"event\":\"state_write\"";
                        json << ",\"event_index\":" << event.event_index;
                        json << ",\"target\":\"0x" << std::hex
                             << config.monitor.current_state_write_polyhook_target
                             << std::dec << "\"";
                        json << ",\"state_comp\":\"0x" << std::hex << event.state_comp
                             << std::dec << "\"";
                        json << ",\"new_state\":\"0x" << std::hex << event.new_state
                             << std::dec << "\"";
                        json << ",\"replacement_state\":\"0x" << std::hex
                             << event.replacement_state << std::dec << "\"";
                        json << ",\"prewrite_replaced\":"
                             << (event.replaced ? "true" : "false");
                        json << ",\"prewrite_replace_count\":"
                             << config.monitor.runtime_sora_wallrun_prewrite_replace_count;
                        json << ",\"effective_state\":\"0x" << std::hex
                             << effective_state_address << std::dec << "\"";
                        json << ",\"state_comp_real\":"
                             << (state_comp_real ? "true" : "false");
                        json << ",\"current_state_real\":"
                             << (current_state_real ? "true" : "false");
                        json << ",\"state_comp_matches_cached_pawn\":"
                             << (state_comp_matches_cached_pawn ? "true" : "false");
                        json << ",\"new_state_class\":\""
                             << json_escape(observed_state_class) << "\"";
                        json << ",\"state_class\":\"" << json_escape(state_class) << "\"";
                        json << ",\"catalog_match\":"
                             << (state_catalog_entry ? "true" : "false");
                        json << ",\"current_state_id\":"
                             << static_cast<int>(*current_state_id);
                        json << "}\n";
                        append_text_file(
                                *event_bridge_dir / channels::current_state_write_polyhook_events,
                                json.str());
                    }
                }
            }
        }

        auto invalidate_cached_sora_state(
                const RuntimeSoraStateRevalidationSnapshot& snapshot,
                RuntimeSoraStateRevalidationResult result) -> void
        {
            if (config.observe_cinematic_transition &&
                runtime_sora_state_matches_kind(
                        RuntimeSoraStateKind::Cinematic,
                        config.monitor.cached_runtime_sora_current_state_id,
                        config.monitor.cached_runtime_sora_state_class))
            {
                config.observe_cinematic_transition(false);
            }
            const bool invalidate_state_comp =
                    runtime_sora_state_revalidation_invalidates_state_comp(result);
            bool cache_changed = false;
            {
                std::lock_guard<std::mutex> lock{
                        config.monitor.current_state_write_polyhook_event_mutex};
                cache_changed =
                        config.monitor.cached_runtime_sora_current_state != nullptr ||
                        config.monitor.cached_runtime_sora_current_state_valid ||
                        config.monitor.cached_runtime_sora_current_state_id.has_value() ||
                        !config.monitor.cached_runtime_sora_state_class.empty() ||
                        config.monitor.cached_runtime_sora_idle_state != nullptr ||
                        config.monitor.cached_runtime_sora_idle_state_valid ||
                        config.monitor.cached_runtime_sora_wallrun_state != nullptr ||
                        config.monitor.cached_runtime_sora_wallrun_state_valid ||
                        (invalidate_state_comp &&
                         (config.monitor.cached_runtime_sora_state_comp != nullptr ||
                          config.monitor.cached_runtime_sora_state_comp_valid ||
                          config.monitor.cached_runtime_sora_state_comp_address != 0));
                if (!cache_changed)
                {
                    return;
                }

                config.monitor.cached_runtime_sora_current_state = nullptr;
                config.monitor.cached_runtime_sora_current_state_valid = false;
                config.monitor.cached_runtime_sora_current_state_id = std::nullopt;
                config.monitor.cached_runtime_sora_state_class.clear();
                config.monitor.cached_runtime_sora_current_state_address = 0;

                // Idle and wall-run pointers participate in the native pre-write
                // clamp, so revoke them whenever the authoritative state can no
                // longer be read. This prevents a stale replacement pointer from
                // surviving the same component teardown.
                config.monitor.cached_runtime_sora_idle_state = nullptr;
                config.monitor.cached_runtime_sora_idle_state_valid = false;
                config.monitor.cached_runtime_sora_idle_state_address = 0;
                config.monitor.cached_runtime_sora_wallrun_state = nullptr;
                config.monitor.cached_runtime_sora_wallrun_state_valid = false;
                config.monitor.cached_runtime_sora_wallrun_state_address = 0;

                if (invalidate_state_comp)
                {
                    config.monitor.cached_runtime_sora_state_comp = nullptr;
                    config.monitor.cached_runtime_sora_state_comp_valid = false;
                    config.monitor.cached_runtime_sora_pawn_address = 0;
                    config.monitor.cached_runtime_sora_state_comp_address = 0;
                }
                config.monitor.cached_runtime_sora_state_source = std::format(
                        STR("periodic_revalidation:{}"),
                        runtime_sora_state_revalidation_result_label(result));
            }

            sidecar_update_pending = true;
            if (config.observe_shop_session)
            {
                config.observe_shop_session(false);
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Runtime Sora state cache invalidated result={} pawn=0x{:X} cached_pawn=0x{:X} state_comp=0x{:X} state_comp_outer=0x{:X} current_state=0x{:X}\n"),
                    runtime_sora_state_revalidation_result_label(result),
                    snapshot.active_pawn,
                    snapshot.cached_pawn,
                    snapshot.state_comp,
                    snapshot.state_comp_outer,
                    snapshot.current_state);
        }

        auto apply_revalidated_sora_state(
                const RuntimeSoraStateRevalidationSnapshot& snapshot,
                RuntimeSoraStateRevalidationResult result) -> void
        {
            const auto* const catalog_entry =
                    lookup_runtime_sora_state_by_id(*snapshot.current_state_id);
            const std::wstring state_class = catalog_entry
                    ? std::wstring{catalog_entry->class_name}
                    : std::wstring{};
            const bool was_cinematic = runtime_sora_state_matches_kind(
                    RuntimeSoraStateKind::Cinematic,
                    snapshot.cached_current_state_id,
                    config.monitor.cached_runtime_sora_state_class);
            const bool is_cinematic = runtime_sora_state_matches_kind(
                    RuntimeSoraStateKind::Cinematic,
                    snapshot.current_state_id,
                    state_class);
            {
                std::lock_guard<std::mutex> lock{
                        config.monitor.current_state_write_polyhook_event_mutex};
                config.monitor.cached_runtime_sora_state_comp =
                        reinterpret_cast<UObject*>(snapshot.state_comp);
                config.monitor.cached_runtime_sora_current_state =
                        reinterpret_cast<UObject*>(snapshot.current_state);
                config.monitor.cached_runtime_sora_state_comp_valid = true;
                config.monitor.cached_runtime_sora_current_state_valid = true;
                config.monitor.cached_runtime_sora_current_state_id =
                        snapshot.current_state_id;
                config.monitor.cached_runtime_sora_state_class = state_class;
                config.monitor.cached_runtime_sora_state_source =
                        STR("periodic_revalidation");
                config.monitor.cached_runtime_sora_pawn_address = snapshot.active_pawn;
                config.monitor.cached_runtime_sora_state_comp_address = snapshot.state_comp;
                config.monitor.cached_runtime_sora_current_state_address =
                        snapshot.current_state;
                if (config.observe_cinematic_transition &&
                    was_cinematic != is_cinematic)
                {
                    config.observe_cinematic_transition(is_cinematic);
                }
                if (runtime_sora_state_matches_kind(
                            RuntimeSoraStateKind::Idle,
                            snapshot.current_state_id,
                            state_class))
                {
                    config.monitor.cached_runtime_sora_idle_state =
                            reinterpret_cast<UObject*>(snapshot.current_state);
                    config.monitor.cached_runtime_sora_idle_state_valid = true;
                    config.monitor.cached_runtime_sora_idle_state_address =
                            snapshot.current_state;
                }
                if (runtime_sora_state_matches_kind(
                            RuntimeSoraStateKind::WallRun,
                            snapshot.current_state_id,
                            state_class))
                {
                    config.monitor.cached_runtime_sora_wallrun_state =
                            reinterpret_cast<UObject*>(snapshot.current_state);
                    config.monitor.cached_runtime_sora_wallrun_state_valid = true;
                    config.monitor.cached_runtime_sora_wallrun_state_address =
                            snapshot.current_state;
                }
            }

            sidecar_update_pending = true;
            handle_direct_player_bad_status_state_transition(
                    reinterpret_cast<UObject*>(snapshot.active_pawn),
                    reinterpret_cast<UObject*>(snapshot.state_comp),
                    reinterpret_cast<UObject*>(snapshot.current_state),
                    static_cast<int32_t>(*snapshot.current_state_id),
                    config.runtime_state);
            if (config.activate_blocking_ui)
            {
                config.activate_blocking_ui(
                        state_class,
                        snapshot.current_state_id);
            }
            if (config.observe_shop_session)
            {
                config.observe_shop_session(runtime_sora_state_matches_kind(
                        RuntimeSoraStateKind::ShopMenu,
                        snapshot.current_state_id,
                        state_class));
            }
            if (config.observe_weapon_change &&
                runtime_sora_state_matches_kind(
                        RuntimeSoraStateKind::WeaponChange,
                        snapshot.current_state_id,
                        state_class))
            {
                config.observe_weapon_change();
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Runtime Sora state cache revalidated result={} old_state=0x{:X} old_state_id={} new_state=0x{:X} new_state_id={} new_state_class={}\n"),
                    runtime_sora_state_revalidation_result_label(result),
                    snapshot.cached_current_state,
                    snapshot.cached_current_state_id.has_value()
                            ? std::to_wstring(*snapshot.cached_current_state_id)
                            : std::wstring{STR("<none>")},
                    snapshot.current_state,
                    static_cast<uint64_t>(*snapshot.current_state_id),
                    state_class.empty()
                            ? std::wstring{STR("<uncataloged>")}
                            : state_class);
        }

        auto revalidate_cached_sora_state() -> void
        {
            auto& pawn_state = config.runtime_state.grant_state().pawn_state();
            RuntimeSoraStateRevalidationSnapshot snapshot{
                    .active_pawn = pawn_state.candidate_trusted()
                            ? reinterpret_cast<uintptr_t>(pawn_state.object())
                            : 0,
            };
            {
                std::lock_guard<std::mutex> lock{
                        config.monitor.current_state_write_polyhook_event_mutex};
                snapshot.cached_pawn =
                        config.monitor.cached_runtime_sora_pawn_address;
                snapshot.state_comp =
                        config.monitor.cached_runtime_sora_state_comp_address;
                snapshot.cached_current_state =
                        config.monitor.cached_runtime_sora_current_state_address;
                snapshot.cached_current_state_id =
                        config.monitor.cached_runtime_sora_current_state_id;
            }

            if (snapshot.active_pawn != 0 &&
                snapshot.cached_pawn != 0 &&
                snapshot.state_comp != 0)
            {
                snapshot.state_comp_outer = static_cast<uintptr_t>(
                        try_read_process_u64(
                                snapshot.state_comp +
                                runtime_sora_state_comp_outer_offset)
                                .value_or(0));
                if (snapshot.state_comp_outer == snapshot.active_pawn)
                {
                    snapshot.current_state = static_cast<uintptr_t>(
                            try_read_process_u64(
                                    snapshot.state_comp +
                                    runtime_sora_current_state_pointer_offset)
                                    .value_or(0));
                    if (snapshot.current_state != 0)
                    {
                        snapshot.current_state_id = try_read_process_u8(
                                snapshot.current_state +
                                runtime_sora_current_state_id_offset);
                        snapshot.catalog_match =
                                snapshot.current_state_id.has_value() &&
                                lookup_runtime_sora_state_by_id(
                                        *snapshot.current_state_id) != nullptr;
                    }
                }
            }

            const auto result = classify_runtime_sora_state_revalidation(snapshot);
            if (result == RuntimeSoraStateRevalidationResult::Changed ||
                result == RuntimeSoraStateRevalidationResult::CatalogMiss)
            {
                apply_revalidated_sora_state(snapshot, result);
                return;
            }
            if (runtime_sora_state_revalidation_requires_cache_invalidation(result))
            {
                invalidate_cached_sora_state(snapshot, result);
            }
        }

        auto write_sidecar_on_change() -> void
        {
            if constexpr (!s_enable_runtime_sora_state_sidecar)
            {
                return;
            }

            const auto& pawn_state =
                    config.runtime_state.grant_state().pawn_state();
            UObject* const pawn =
                    pawn_state.object() &&
                            pawn_state.candidate_trusted()
                    ? pawn_state.object()
                    : nullptr;
            const auto resolved_bridge_dir = bridge_dir();
            const auto runtime_context_snapshot =
                    config.runtime_state.context_subsystem_state()
                            .context_state()
                            .current_snapshot();
            const std::wstring resolved_runtime_context = runtime_context_summary(
                    runtime_context_snapshot,
                    resolved_bridge_dir);
            write_runtime_sora_state_sidecar_on_change(
                    last_sidecar_signature,
                    RuntimeSoraStateSidecarDeps{
                            .bridge_dir = resolved_bridge_dir,
                            .pawn = pawn,
                            .current_state_cache = RuntimeSoraCurrentStateCacheView{
                                    .available = true,
                                    .state_comp = config.monitor.cached_runtime_sora_state_comp,
                                    .current_state = config.monitor.cached_runtime_sora_current_state,
                                    .state_comp_valid =
                                            config.monitor.cached_runtime_sora_state_comp_valid,
                                    .current_state_valid =
                                            config.monitor.cached_runtime_sora_current_state_valid,
                                    .current_state_id =
                                            config.monitor.cached_runtime_sora_current_state_id,
                                    .state_class =
                                            config.monitor.cached_runtime_sora_state_class,
                                    .state_source =
                                            config.monitor.cached_runtime_sora_state_source,
                                    .pawn_address =
                                            config.monitor.cached_runtime_sora_pawn_address,
                                    .polyhook_installed =
                                            config.monitor.current_state_write_polyhook_installed,
                                    .polyhook_target =
                                            config.monitor.current_state_write_polyhook_target,
                                    .event_index =
                                            config.monitor.cached_runtime_sora_state_event_index,
                                    .state_comp_address =
                                            config.monitor.cached_runtime_sora_state_comp_address},
                            .protocol_name = s_bridge_protocol_name,
                            .protocol_version = s_bridge_protocol_version,
                            .runtime_context = resolved_runtime_context,
                            .runtime_context_snapshot = runtime_context_snapshot.has_value()
                                    ? &*runtime_context_snapshot
                                    : nullptr,
                            .special_battle_mode_active =
                                    config.runtime_state.grant_state().special_battle_mode_active(),
                            .special_battle_mode_source =
                                    config.runtime_state.grant_state().special_battle_mode_source(),
                            .special_battle_mode_details =
                                    config.runtime_state.grant_state().special_battle_mode_details(),
                            .special_battle_mode_map_root =
                                    config.runtime_state.grant_state().special_battle_mode_map_root(),
                            .event_log_enabled = s_enable_runtime_sora_state_event_log,
                            .activate_blocking_ui_from_sora_state =
                                    [this](
                                            std::wstring_view state_class,
                                            std::optional<uint8_t> current_state_id) {
                                        if (config.activate_blocking_ui)
                                        {
                                            config.activate_blocking_ui(
                                                    state_class,
                                                    current_state_id);
                                        }
                                    }});
        }

        auto process_sidecar(
                std::chrono::steady_clock::time_point now,
                bool runtime_file_polling_ready) -> void
        {
            if constexpr (!s_enable_runtime_sora_state_sidecar)
            {
                return;
            }

            bool should_write = false;
            if (runtime_file_polling_ready)
            {
                if (should_run_periodic_update(
                            last_state_revalidation_poll,
                            now,
                            s_runtime_sora_state_sidecar_dirty_interval))
                {
                    revalidate_cached_sora_state();
                }
                if (sidecar_update_pending)
                {
                    should_write = should_run_periodic_update(
                            last_sidecar_poll,
                            now,
                            s_runtime_sora_state_sidecar_dirty_interval);
                    if (should_write)
                    {
                        sidecar_update_pending = false;
                    }
                }
                else
                {
                    should_write = should_run_periodic_update(
                            last_sidecar_poll,
                            now,
                            s_runtime_sora_state_sidecar_fallback_interval);
                }
            }
            if (should_write)
            {
                write_sidecar_on_change();
            }
        }

        RuntimeSoraStateHookConfig config;
        bool sidecar_update_pending{true};
        std::chrono::steady_clock::time_point last_state_revalidation_poll{};
        std::chrono::steady_clock::time_point last_sidecar_poll{};
        std::wstring last_sidecar_signature{};
        std::optional<std::filesystem::path> cached_bridge_dir_value{};
    };

    RuntimeSoraStateHookService::RuntimeSoraStateHookService(
            RuntimeSoraStateHookConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeSoraStateHookService::~RuntimeSoraStateHookService() = default;
    RuntimeSoraStateHookService::RuntimeSoraStateHookService(
            RuntimeSoraStateHookService&&) noexcept = default;
    auto RuntimeSoraStateHookService::operator=(
            RuntimeSoraStateHookService&&) noexcept
            -> RuntimeSoraStateHookService& = default;

    auto RuntimeSoraStateHookService::install_hooks(std::wstring_view reason) -> void
    {
        impl_->config.memory_hooks.install_vanilla_level_write_polyhook_once(reason);
        impl_->config.memory_hooks.install_current_state_write_polyhook_once(reason);
        impl_->config.memory_hooks.install_lucky_mark_native_ap_resolver_once(reason);
    }

    auto RuntimeSoraStateHookService::uninstall_hooks() -> void
    {
        impl_->config.memory_hooks.uninstall_lucky_mark_native_ap_resolver();
        impl_->config.memory_hooks.uninstall_current_state_write_polyhook();
        impl_->config.memory_hooks.uninstall_vanilla_level_write_polyhook();
    }

    auto RuntimeSoraStateHookService::flush_current_state_events() -> void
    {
        impl_->flush_current_state_events();
    }

    auto RuntimeSoraStateHookService::flush_vanilla_level_events() -> void
    {
        impl_->flush_vanilla_level_events();
    }

    auto RuntimeSoraStateHookService::mark_sidecar_dirty() -> void
    {
        impl_->sidecar_update_pending = true;
    }

    auto RuntimeSoraStateHookService::process_sidecar(
            std::chrono::steady_clock::time_point now,
            bool runtime_file_polling_ready) -> void
    {
        impl_->process_sidecar(now, runtime_file_polling_ready);
    }
}
