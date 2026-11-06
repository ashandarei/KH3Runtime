#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <polyhook2/Detour/x64Detour.hpp>

#include "kh3ap/app/update_coordinator.h"

#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_output_helpers.h"
#include "kh3ap/app/periodic_bridge_service.h"
#include "kh3ap/features/story/quality_of_life_service.h"
#include "kh3ap/features/story/cutscenes/shoot_flow_restore_helpers.h"
#include "kh3ap/features/player/state/sora_level_monitor_state.h"
#include "kh3ap/features/player/state/sora_state_hook_service.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/story/progression/story_progress_service.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap_service.h"
#include "kh3ap/bridge/utilities.h"

#include <utility>

#include <UnrealDef.hpp>

namespace kh3ap
{
    namespace
    {
        constexpr auto heartbeat_check_interval = std::chrono::seconds(1);
        constexpr auto heartbeat_log_interval = std::chrono::seconds(5);

        using ::kh3ap::settings::s_enable_he02_shoot_flow_restore;
        using ::kh3ap::settings::s_enable_runtime_file_polling;
        using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;
        using ::kh3ap::settings::s_enable_runtime_update_loop;
        using ::kh3ap::settings::s_runtime_file_poll_startup_delay;
    }

    struct RuntimeUpdateCoordinator::Impl
    {
        explicit Impl(RuntimeUpdateCoordinatorConfig config)
            : config(std::move(config))
        {
        }

        auto tick(
                bool unreal_initialized,
                bool title_or_ui_controller_active,
                bool ignored_map_active,
                std::chrono::steady_clock::time_point unreal_init_time,
                const PhaseMarker& mark_phase) -> void
        {
            if constexpr (!s_enable_runtime_update_loop)
            {
                return;
            }
            if (!unreal_initialized)
            {
                return;
            }

            mark_phase(STR("start"));
            const auto now = std::chrono::steady_clock::now();
            if (title_or_ui_controller_active)
            {
                // No runtime grant, bridge command, pawn, or gameplay-state
                // work is actionable on the title screen.
                mark_phase(STR("title_fast_path"));
                return;
            }

            if constexpr (s_enable_he02_shoot_flow_restore)
            {
                config.shoot_flow_restore.tick(
                        config.sora_monitor.cached_runtime_sora_state_class);
            }
            mark_phase(STR("he02_shoot_flow_restore"));

            if (config.refresh_retry_blueprint_targets)
            {
                config.refresh_retry_blueprint_targets();
            }
            mark_phase(STR("retry_blueprint_target_refresh"));
            if (config.flush_retry_blueprint_events)
            {
                config.flush_retry_blueprint_events();
            }
            mark_phase(STR("retry_blueprint_probe_flush"));
            if (config.flush_retry_point_events)
            {
                config.flush_retry_point_events();
            }
            mark_phase(STR("retry_point_probe_flush"));

            if (ignored_map_active)
            {
                // Native write detours cannot cheaply resolve an Unreal map on
                // their writer thread. Discard transition-map observations so
                // they cannot be replayed after travel.
                if (config.drain_runtime_object_observations)
                {
                    config.drain_runtime_object_observations(true);
                }
                config.memory_hooks.drain_current_state_write_events();
                config.memory_hooks.drain_vanilla_level_write_events();
                return;
            }

            if (config.drain_runtime_object_observations)
            {
                config.drain_runtime_object_observations(false);
            }
            mark_phase(STR("treasure_observations"));

            if (config.process_lucky_emblem_tracking)
            {
                config.process_lucky_emblem_tracking(now);
            }
            mark_phase(STR("lucky_emblem_tracking"));

            if (config.process_union_cross_completion)
            {
                config.process_union_cross_completion();
            }
            mark_phase(STR("union_cross_completion"));

            if (config.process_beat_of_lead_completion)
            {
                config.process_beat_of_lead_completion();
            }
            mark_phase(STR("beat_of_lead_completion"));

            config.story_progress.process_guardians_of_light_skip(now);
            mark_phase(STR("guardians_of_light_slow_mickey_skip"));

            config.quality_of_life.tick(now);
            mark_phase(STR("native_quality_of_life"));

            config.world_bootstrap.tick(now);
            mark_phase(STR("native_world_bootstrap"));

            if (config.autosave_probe.needs_update(now))
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:autosave_probe")};
                config.autosave_probe.update(now);
                // Only save-completion handlers enqueue this work, and they
                // run above. Preserve their same-tick GoA autosave dispatch.
                if (config.process_pending_goa_autosave)
                {
                    config.process_pending_goa_autosave();
                }
            }
            mark_phase(STR("autosave_probe"));

            log_runtime_update_heartbeat_if_needed(
                    config.runtime_state,
                    now,
                    heartbeat_check_interval,
                    heartbeat_log_interval);
            mark_phase(STR("heartbeat"));

            config.sora_state_hooks.flush_current_state_events();
            mark_phase(STR("current_state_flush"));

            config.sora_state_hooks.flush_vanilla_level_events();
            mark_phase(STR("level_event_flush"));
            config.battle_level_sync.process_pending(now);
            mark_phase(STR("level_write_flush"));

            if (config.process_controller_reacquire)
            {
                config.process_controller_reacquire(now);
            }
            mark_phase(STR("controller_reacquire"));

            config.story_progress.process_caribbean_chart_unlock(now);
            mark_phase(STR("caribbean_chart_visit_flags"));

            const bool runtime_file_polling_ready =
                    s_enable_runtime_file_polling &&
                    has_elapsed_since(
                            unreal_init_time,
                            now,
                            s_runtime_file_poll_startup_delay);
            mark_phase(STR("runtime_file_polling_ready"));

            config.sora_state_hooks.process_sidecar(now, runtime_file_polling_ready);
            mark_phase(STR("runtime_sora_state_sidecar"));

            config.periodic_bridge.process(
                    now,
                    runtime_file_polling_ready,
                    mark_phase);
        }

        RuntimeUpdateCoordinatorConfig config;
    };

    RuntimeUpdateCoordinator::RuntimeUpdateCoordinator(
            RuntimeUpdateCoordinatorConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeUpdateCoordinator::~RuntimeUpdateCoordinator() = default;
    RuntimeUpdateCoordinator::RuntimeUpdateCoordinator(
            RuntimeUpdateCoordinator&&) noexcept = default;
    auto RuntimeUpdateCoordinator::operator=(
            RuntimeUpdateCoordinator&&) noexcept
            -> RuntimeUpdateCoordinator& = default;

    auto RuntimeUpdateCoordinator::tick(
            bool unreal_initialized,
            bool title_or_ui_controller_active,
            bool ignored_map_active,
            std::chrono::steady_clock::time_point unreal_init_time,
            const PhaseMarker& mark_phase) -> void
    {
        impl_->tick(
                unreal_initialized,
                title_or_ui_controller_active,
                ignored_map_active,
                unreal_init_time,
                mark_phase);
    }
}
