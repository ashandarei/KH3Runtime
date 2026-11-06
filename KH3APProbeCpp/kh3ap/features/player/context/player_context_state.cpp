#include "kh3ap/app/config.h"
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_PLAYER_CONTEXT_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_PLAYER_CONTEXT_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_PLAYER_CONTEXT_RESTORE_S
#endif
#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/features/player/state/sora_state_revalidation.h"
#include "kh3ap/features/player/state/sora_state_hook_service.h"
#include "kh3ap/features/player/state/sora_level_monitor_state.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <chrono>
#include <format>
#include <mutex>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;
    namespace
    {
        using ::kh3ap::settings::s_enable_current_state_write_polyhook;
    }

    auto RuntimePlayerContextService::Impl::bootstrap_sora_state_from_trusted_pawn(
        UObject* pawn, std::wstring_view source) -> bool
    {
        if (!pawn || config.runtime_state.grant_state().pawn_state().object() != pawn ||
            !config.runtime_state.grant_state().pawn_state().candidate_trusted())
        {
            return false;
        }

        auto& monitor = config.sora_monitor;
        const uintptr_t pawn_address = reinterpret_cast<uintptr_t>(pawn);
        if (monitor.cached_runtime_sora_pawn_address == pawn_address &&
            monitor.cached_runtime_sora_state_comp_valid &&
            monitor.cached_runtime_sora_current_state_valid &&
            monitor.cached_runtime_sora_state_comp && monitor.cached_runtime_sora_current_state)
        {
            return true;
        }

        const auto started_at = std::chrono::steady_clock::now();
        UObject* const state_comp = read_object_property_in_chain(pawn, STR("MyStateComp"));
        if (!state_comp || !kh3ap::is_uobject_valid_fast(state_comp))
        {
            return false;
        }
        UObject* state_comp_outer = nullptr;
        try
        {
            state_comp_outer = state_comp->GetOuterPrivate();
        }
        catch (...)
        {
            state_comp_outer = nullptr;
        }
        if (state_comp_outer != pawn)
        {
            return false;
        }

        FProperty* const current_state_property =
            state_comp->GetPropertyByNameInChain(STR("m_pCurrentState"));
        UObject* const current_state =
            read_object_property_in_chain(state_comp, STR("m_pCurrentState"));
        if (!current_state_property || !current_state ||
            !kh3ap::is_uobject_valid_fast(current_state))
        {
            return false;
        }

        const std::optional<uint8_t> current_state_id =
            read_u8_object_property(current_state, STR("MyStateID"));
        const std::wstring state_class = format_object_class_name(current_state);
        const RuntimeSoraStateCatalogEntry* catalog_entry =
            current_state_id.has_value() ? lookup_runtime_sora_state_by_id(*current_state_id)
                                         : nullptr;
        if (!catalog_entry && !state_class.empty())
        {
            catalog_entry = lookup_runtime_sora_state_by_class_name(state_class);
        }
        // A replacement pawn can already be in an uncatalogued battle
        // state (kg_05 uses state id 42). Preserve that live identity as a
        // known-but-unsafe state instead of leaving the previous pawn's
        // cinematic state cached.
        if (!current_state_id.has_value() && !catalog_entry)
        {
            return false;
        }

        monitor.cached_runtime_sora_state_comp = state_comp;
        monitor.cached_runtime_sora_current_state = current_state;
        monitor.cached_runtime_sora_state_comp_valid = true;
        monitor.cached_runtime_sora_current_state_valid = true;
        monitor.cached_runtime_sora_current_state_id =
            current_state_id.has_value() ? current_state_id
                                         : std::optional<uint8_t>{catalog_entry->id};
        monitor.cached_runtime_sora_state_class =
            state_class.empty()
                ? (catalog_entry ? std::wstring{catalog_entry->class_name} : std::wstring{})
                : state_class;
        monitor.cached_runtime_sora_state_source =
            std::format(STR("trusted_pawn_bootstrap:{}"), source);
        monitor.cached_runtime_sora_state_event_index = 0;
        monitor.cached_runtime_sora_pawn_address = pawn_address;
        monitor.cached_runtime_sora_state_comp_address = reinterpret_cast<uintptr_t>(state_comp);
        monitor.cached_runtime_sora_current_state_address =
            reinterpret_cast<uintptr_t>(current_state);
        if (runtime_sora_state_matches_kind(RuntimeSoraStateKind::Idle,
                                            monitor.cached_runtime_sora_current_state_id,
                                            monitor.cached_runtime_sora_state_class))
        {
            std::lock_guard<std::mutex> lock{monitor.current_state_write_polyhook_event_mutex};
            monitor.cached_runtime_sora_idle_state = current_state;
            monitor.cached_runtime_sora_idle_state_valid = true;
            monitor.cached_runtime_sora_idle_state_address =
                reinterpret_cast<uintptr_t>(current_state);
        }
        config.sora_state_hooks.mark_sidecar_dirty();
        const uint64_t elapsed_us =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - started_at)
                                      .count());
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Runtime Sora state cache bootstrapped source={} pawn=0x{:X} "
                "state_comp=0x{:X} current_state=0x{:X} state_id={} state_class={} "
                "elapsed_us={}\n"),
            source, pawn_address, reinterpret_cast<uintptr_t>(state_comp),
            reinterpret_cast<uintptr_t>(current_state),
            monitor.cached_runtime_sora_current_state_id.has_value()
                ? std::to_wstring(*monitor.cached_runtime_sora_current_state_id)
                : std::wstring{STR("<none>")},
            monitor.cached_runtime_sora_state_class, elapsed_us);
        return true;
    }

    auto
    RuntimePlayerContextService::Impl::revalidate_sora_state_from_event(std::wstring_view source,
                                                                        bool log_unchanged) -> bool
    {
        if constexpr (!s_enable_current_state_write_polyhook)
        {
            return false;
        }

        auto& monitor = config.sora_monitor;
        const uintptr_t active_pawn =
            reinterpret_cast<uintptr_t>(config.runtime_state.grant_state().pawn_state().object());
        const uintptr_t cached_pawn = monitor.cached_runtime_sora_pawn_address;
        const uintptr_t state_comp = monitor.cached_runtime_sora_state_comp_address;
        RuntimeSoraStateRevalidationSnapshot snapshot{
            .active_pawn = active_pawn,
            .cached_pawn = cached_pawn,
            .state_comp = state_comp,
            .cached_current_state = monitor.cached_runtime_sora_current_state_address,
            .cached_current_state_id = monitor.cached_runtime_sora_current_state_id,
        };
        if (active_pawn != 0 && cached_pawn != 0 && state_comp != 0)
        {
            snapshot.state_comp_outer =
                try_read_process_u64(state_comp + runtime_sora_state_comp_outer_offset).value_or(0);
            if (snapshot.state_comp_outer == active_pawn)
            {
                snapshot.current_state =
                    try_read_process_u64(state_comp + runtime_sora_current_state_pointer_offset)
                        .value_or(0);
                if (snapshot.current_state != 0)
                {
                    snapshot.current_state_id = try_read_process_u8(
                        snapshot.current_state + runtime_sora_current_state_id_offset);
                    snapshot.catalog_match =
                        snapshot.current_state_id.has_value() &&
                        lookup_runtime_sora_state_by_id(*snapshot.current_state_id) != nullptr;
                }
            }
        }

        const auto result = classify_runtime_sora_state_revalidation(snapshot);
        if (result != RuntimeSoraStateRevalidationResult::Changed)
        {
            if (log_unchanged || result != RuntimeSoraStateRevalidationResult::Unchanged)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Runtime Sora state event revalidation source={} result={} "
                        "pawn=0x{:X} cached_pawn=0x{:X} state_comp=0x{:X} state_comp_outer=0x{:X} "
                        "current_state=0x{:X} cached_current_state=0x{:X}\n"),
                    source, runtime_sora_state_revalidation_result_label(result), active_pawn,
                    cached_pawn, state_comp, snapshot.state_comp_outer, snapshot.current_state,
                    snapshot.cached_current_state);
            }
            return false;
        }

        const auto* const catalog_entry =
            lookup_runtime_sora_state_by_id(*snapshot.current_state_id);
        monitor.cached_runtime_sora_state_comp = reinterpret_cast<UObject*>(state_comp);
        monitor.cached_runtime_sora_current_state =
            reinterpret_cast<UObject*>(snapshot.current_state);
        monitor.cached_runtime_sora_state_comp_valid = true;
        monitor.cached_runtime_sora_current_state_valid = true;
        monitor.cached_runtime_sora_current_state_id = snapshot.current_state_id;
        monitor.cached_runtime_sora_state_class = std::wstring{catalog_entry->class_name};
        monitor.cached_runtime_sora_state_source =
            std::format(STR("event_revalidation:{}"), source);
        monitor.cached_runtime_sora_pawn_address = active_pawn;
        monitor.cached_runtime_sora_state_comp_address = state_comp;
        monitor.cached_runtime_sora_current_state_address = snapshot.current_state;
        config.sora_state_hooks.mark_sidecar_dirty();
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Runtime Sora state event revalidation source={} result=changed "
                "old_state=0x{:X} old_state_id={} new_state=0x{:X} new_state_id={} "
                "new_state_class={}\n"),
            source, snapshot.cached_current_state,
            snapshot.cached_current_state_id.has_value()
                ? std::to_wstring(*snapshot.cached_current_state_id)
                : std::wstring{STR("<none>")},
            snapshot.current_state, static_cast<uint64_t>(*snapshot.current_state_id),
            catalog_entry->class_name);
        return true;
    }
} // namespace kh3ap
