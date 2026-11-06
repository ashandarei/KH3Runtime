#include "kh3ap/app/config.h"
#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/grants/ap_ignored_map.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/AActor.hpp>
#include <chrono>
#include <format>
#include <sstream>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;
    namespace
    {
        using ::kh3ap::settings::s_bridge_protocol_name;
        using ::kh3ap::settings::s_bridge_protocol_version;
        using ::kh3ap::settings::s_enable_hold_camera_runtime_grant_gate_probe_hooks;
        using ::kh3ap::settings::s_enable_runtime_item_grant_polling;
        using ::kh3ap::settings::s_enable_shootlock_runtime_grant_gate;
        using ::kh3ap::settings::s_runtime_item_grant_delay;
        using ::kh3ap::settings::s_runtime_item_grant_special_battle_exit_delay;
    }

    auto RuntimePlayerContextService::Impl::publish_playable_context_from_validated_pawn_name(
        UObject* pawn, std::wstring_view pawn_name, std::chrono::steady_clock::time_point now,
        std::wstring_view pawn_source) -> void
    {
        if (is_runtime_ap_ignored_map_context(pawn_name))
        {
            if (config.enter_ignored_map)
            {
                config.enter_ignored_map(pawn_name, pawn_source);
            }
            return;
        }
        auto snapshot = derive_runtime_context_from_object_full_name(
            pawn_name, STR("RuntimePlayablePawnContext"));
        if (!snapshot.has_value())
        {
            return;
        }
        snapshot->pawn_source = std::wstring{pawn_source};

        auto& context_state = config.runtime_state.context_subsystem_state().context_state();
        if (context_state.should_skip_playable_publish_for_object(
                pawn, now, RuntimeContextState::playable_republish_interval))
        {
            return;
        }
        const std::wstring context_key =
            snapshot->hook + STR("|") + snapshot->world + STR("|") + std::wstring{pawn_name};
        if (context_state.should_skip_playable_publish(
                context_key, now, RuntimeContextState::playable_republish_interval))
        {
            return;
        }

        context_state.mark_playable_published(context_key, now, pawn);
        RuntimeObjectResolver::cache_world_context_object(config.runtime_state.object_cache(),
                                                          pawn);
        context_state.set_current_snapshot(*snapshot, snapshot->outer);
        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Runtime context playable-pawn refresh "
                                            "pawn={} | source={} | world={} | outer={}\n"),
                                        pawn_name, pawn_source, snapshot->world, snapshot->outer);
        write_runtime_context_file(bridge_dir(), *snapshot);
    }

    auto RuntimePlayerContextService::Impl::trusted_current_playable_cached_sora_pawn() -> UObject*
    {
        const RuntimeHotPathProfileScope profile{L"player_context:current_possession"};
        static ReflectedPropertyCache<4> possession_properties;
        auto& pawn_state = config.runtime_state.grant_state().pawn_state();
        // The opening encounter replaces its cinematic pawn without a map
        // jump. Follow the already verified, weak controller's possession.
        UObject* controller = pawn_state.controller_object();
        if (!controller && local_player.Get())
        {
            controller =
                possession_properties.read_object(local_player.Get(), STR("PlayerController"));
            if (!kh3ap::is_uobject_valid_fast(controller))
            {
                controller = nullptr;
            }
        }
        UObject* const possessed = possession_properties.read_object(controller, STR("Pawn"));
        if (kh3ap::is_uobject_valid_fast(possessed) && possessed != pawn_state.object() &&
            possession_properties.read_object(possessed, STR("Controller")) == controller)
        {
            cache_pawn_from_player_controller(controller, STR("CurrentPossession"), {}, true);
        }
        UObject* const cached_pawn = pawn_state.object();
        if (!cached_pawn || !pawn_state.candidate_trusted())
        {
            return nullptr;
        }
        return cached_pawn;
    }

    auto RuntimePlayerContextService::Impl::mark_trusted_current_playable_context_map_ready(
        std::wstring_view source) -> std::optional<RuntimeContextSnapshot>
    {
        UObject* const cached_pawn = trusted_current_playable_cached_sora_pawn();
        if (!kh3ap::is_uobject_valid_fast(cached_pawn))
        {
            return std::nullopt;
        }
        const std::wstring pawn_name = format_object_debug_name(cached_pawn);
        auto snapshot = derive_runtime_context_from_object_full_name(
            pawn_name, STR("RuntimePlayablePawnContext"));
        if (!snapshot.has_value())
        {
            return std::nullopt;
        }
        auto& context_state = config.runtime_state.context_subsystem_state().context_state();
        snapshot->pawn_source = std::wstring{source};
        const std::wstring context_key =
            snapshot->hook + STR("|") + snapshot->world + STR("|") + pawn_name;
        context_state.mark_playable_published(context_key, std::chrono::steady_clock::now(),
                                              cached_pawn);
        RuntimeObjectResolver::cache_world_context_object(config.runtime_state.object_cache(),
                                                          cached_pawn);
        context_state.set_current_snapshot(*snapshot, snapshot->outer);
        write_runtime_context_file(bridge_dir(), *snapshot);
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime context map-ready promotion reused trusted cached pawn "
                "source={} pawn={} world={}\n"),
            source, snapshot->object, snapshot->world);
        return snapshot;
    }

    auto RuntimePlayerContextService::Impl::write_map_ready_marker(
        std::wstring_view source, UObject* context, UObject* frame_object,
        bool promote_trusted_playable_context) -> bool
    {
        auto snapshot = promote_trusted_playable_context
                            ? mark_trusted_current_playable_context_map_ready(source)
                            : std::optional<RuntimeContextSnapshot>{};
        if (!snapshot.has_value())
        {
            snapshot =
                config.runtime_state.context_subsystem_state().context_state().current_snapshot();
        }
        if (snapshot.has_value())
        {
            snapshot->pawn_source = std::wstring{source};
        }
        if (snapshot.has_value() && snapshot->hook == STR("RuntimePlayablePawnContext") &&
            !snapshot->world.empty() && !snapshot->object.empty())
        {
            const auto ready_key = snapshot->world + STR("|") + snapshot->object;
            auto* const pawn = trusted_current_playable_cached_sora_pawn();
            if (!is_uobject_a_fast(pawn, AActor::StaticClass()))
            {
                return false;
            }
            auto* const world = reinterpret_cast<UObject*>(static_cast<AActor*>(pawn)->GetWorld());
            if (!is_uobject_valid_fast(world))
            {
                return false;
            }
            // ApplyMapName and its animation callback can both fire for an
            // entry, and fire again for area names inside the same world.
            // Keep their UI/grant releases, but publish lifecycle work only
            // once for this live ownership. Weak identities also detect GC
            // address reuse, and reset_playable_context re-arms real travel.
            if (map_ready_context_key == ready_key && map_ready_pawn.Get() == pawn &&
                map_ready_world.Get() == world)
            {
                return false;
            }
            map_ready_pawn = make_uobject_weak(pawn);
            map_ready_world = make_uobject_weak(world);
            map_ready_context_key = ready_key;
            request_music_update(MusicUpdate::Context);
        }
        else
        {
            return false;
        }

        if (config.on_map_ready)
        {
            config.on_map_ready();
        }
        const auto resolved_bridge_dir = bridge_dir();
        if (!resolved_bridge_dir.has_value())
        {
            return true;
        }
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(s_bridge_protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << s_bridge_protocol_version << ",\n";
        json << "  \"schema\": \"kh3ap.runtime_map_ready.v1\",\n";
        json << "  \"event\": \"runtime_map_ready\",\n";
        json << "  \"source\": \"" << json_escape(source) << "\",\n";
        json << "  \"observed_at_utc\": \"" << now_iso8601_utc() << "\",\n";
        if (snapshot.has_value())
        {
            json << "  \"hook\": \"" << json_escape(snapshot->hook) << "\",\n";
            json << "  \"world\": \"" << json_escape(snapshot->world) << "\",\n";
            json << "  \"outer\": \"" << json_escape(snapshot->outer) << "\",\n";
            json << "  \"object\": \"" << json_escape(snapshot->object) << "\",\n";
            json << "  \"pawn_source\": \"" << json_escape(snapshot->pawn_source) << "\",\n";
        }
        json << "  \"context\": \"" << json_escape(format_object_debug_name(context)) << "\",\n";
        json << "  \"frame\": \"" << json_escape(format_object_debug_name(frame_object)) << "\"\n";
        json << "}\n";
        if (!write_text_file(*resolved_bridge_dir / channels::runtime_map_ready, json.str()))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Runtime map ready marker write failed source={} path={}\n"),
                source, (*resolved_bridge_dir / channels::runtime_map_ready_file).wstring());
        }
        return true;
    }

    auto RuntimePlayerContextService::Impl::process_pending_grant_from_runtime_ready_event(
        std::wstring_view reason, bool refresh_playable_context) -> void
    {
        if constexpr (!s_enable_runtime_item_grant_polling ||
                      (!s_enable_hold_camera_runtime_grant_gate_probe_hooks &&
                       !s_enable_shootlock_runtime_grant_gate))
        {
            return;
        }
        if (config.runtime_state.grant_state().item_grant_poll_state().is_armed())
        {
            revalidate_sora_state_from_event(reason, true);
        }

        const auto now = std::chrono::steady_clock::now();
        const bool special_battle_exit = reason == STR("special_battle_mode");
        const auto ready_delay = special_battle_exit
                                     ? s_runtime_item_grant_special_battle_exit_delay
                                     : s_runtime_item_grant_delay;
        const std::wstring playable_context_source =
            std::format(STR("RuntimeReadyEvent:{}"), reason);
        if (!special_battle_exit && refresh_playable_context &&
            config.publish_cached_playable_context)
        {
            config.publish_cached_playable_context(now, playable_context_source);
        }
        if (!special_battle_exit && refresh_playable_context &&
            config.runtime_state.grant_state().clear_classic_kingdom_transition_gate(
                playable_context_source, now))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ClassicKingdom runtime grant gate cleared source={} "
                    "grants_safe=true retry_pending=true ready_event=playable_context "
                    "mode=event_driven\n"),
                playable_context_source);
        }
        if (!special_battle_exit && refresh_playable_context &&
            config.runtime_state.grant_state().clear_olympus_coliseum_transition_gate(
                playable_context_source, now))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OlympusColiseum runtime grant gate cleared source={} "
                    "grants_safe=true retry_pending=true ready_event=playable_context "
                    "mode=event_driven\n"),
                playable_context_source);
        }
        config.runtime_state.grant_state().item_grant_poll_state().arm_ready(ready_delay, now);
        const std::wstring context_summary = runtime_context();
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime item grant ready event reason={} delay_ms={} "
                "game_thread_wake={} | context={}\n"),
            reason,
            static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(ready_delay).count()),
            special_battle_exit ? STR("false") : STR("true"), context_summary);
        if (!special_battle_exit && config.process_pending_grant_now)
        {
            config.process_pending_grant_now(context_summary);
        }
    }
} // namespace kh3ap
