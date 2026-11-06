#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/app/composition/update_trace.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/hooks/patches/kh3_expanded_save_slots_patch.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/hooks/patches/kh3_signed_munny_patch.h"
#include "kh3ap/hooks/patches/kh3_sqex_translucent_slice_view_guard_patch.h"
#include "kh3ap/hooks/kh3_runtime_object_observer.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/ui/map_overlay/minimap_context_service.h"
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/features/player/characters/character_swap_effects.h"
#include "kh3ap/features/player/combat/character_style.h"
#include "kh3ap/features/player/combat/kairi_shining_hud.h"
#include "kh3ap/features/player/characters/character_pawn_pool.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/player/keyblades/keyblade_options_helpers.h"
#include "kh3ap/features/player/objects/map_actor_lifecycle_service.h"
#include "kh3ap/hooks/hook_lifecycle_service.h"
#include "kh3ap/hooks/hook_service.h"
#include "kh3ap/app/startup_service.h"
#include "kh3ap/features/player/objects/object_observation_service.h"
#include "kh3ap/diagnostics/retry/retry_probe_service.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_hook_service.h"
#include "kh3ap/features/colors/color_service.h"
#include "kh3ap/features/player/state/sora_state_hook_service.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/features/story/progression/story_progress_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <filesystem>
#include <format>
#include <functional>
#include <string_view>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
    auto RuntimeComposition::Impl::make_runtime_object_observations()
        -> RuntimeObjectObservationService
    {
        return RuntimeObjectObservationService{RuntimeObjectObservationConfig{
            s_enable_runtime_uobject_create_observer,
            s_enable_static_construct_callback,
            s_enable_open_treasure_player_state_fallback_observer,
            s_enable_open_treasure_char_state_fallback_observer,
            s_enable_sora_pawn_creation_cache,
            s_enable_festival_dance_story_fast_completion,
            s_enable_beat_of_lead_story_fast_completion,
            s_enable_union_cross_immediate_completion,
            s_enable_final_world_ui_bypasses,
            s_enable_olympus_completion_tutorial_bypass,
            s_enable_olympus_early_tutorial_bypasses,
            s_enable_twilight_town_tutorial_bypasses,
            s_enable_olympus_save_points_tutorial_bypass,
            s_enable_caribbean_sea_spectacle_tutorial_bypass,
            s_enable_toy_box_tutorial_bypasses,
            s_enable_link_tutorial_bypasses,
            s_enable_informational_help_bypasses,
            s_enable_lucky_emblem_tracker,
            s_enable_runtime_portal_spawn_request || s_enable_runtime_bx02_portal_auto_spawn ||
                s_enable_runtime_he02_death_mirror_auto_spawn,
            s_enable_runtime_world_bootstrap,
            s_runtime_state.treasure_state().class_cache(),
            s_open_treasure_player_state_class_path,
            s_open_treasure_char_state_class_path,
            s_treasure_logic_actor_class_path,
            []()
            {
                return treasure_runtime_deps();
            },
            [](Kh3RuntimeObjectObservationKind kind)
            {
                record_treasure_route_observation(kind);
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_festival_dance_story.process_constructed_object(object);
                }
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_beat_of_lead.process_constructed_object(object);
                }
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_union_cross_commands.process_constructed_object(object);
                }
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_player_context.cache_constructed_sora_pawn(
                        object, STR("UObjectCreate:p_ex001_Pawn_C"));
                }
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_lucky_emblem_tracker.process_constructed_object(object);
                }
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_map_actor_lifecycle.cache_bx_portal_class(object);
                }
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_map_actor_lifecycle.cache_he02_death_mirror_class(object);
                }
            },
            [this](UObject* object)
            {
                if (m_service_graph_ready)
                {
                    m_world_bootstrap.cache_generated_class(object);
                }
            },
            [this]()
            {
                if (m_service_graph_ready)
                {
                    m_map_overlay.notify_treasure_changed();
                    m_periodic_bridge.notify_treasure_changed();
                }
            },
        }};
    }

    auto RuntimeComposition::Impl::make_retry_probes() -> RuntimeRetryProbeService
    {
        return RuntimeRetryProbeService{RuntimeRetryProbeConfig{
            .blueprint_observer_enabled = s_enable_kh3_retry_blueprint_process_observer,
            .retry_point_direct_hooks_enabled = s_enable_kh3_retry_point_direct_probe_hooks,
            .create_retry_point_hook_enabled = s_enable_kh3_create_retry_point_probe_hook,
            .retry_point_process_event_observer_enabled =
                s_enable_kh3_retry_point_process_event_observer,
            .discard_retry_point_hook_enabled = s_enable_kh3_discard_retry_point_probe_hook,
            .blueprint_state = s_retry_blueprint_probe_state,
            .retry_point_state = s_retry_point_probe_state,
            .retry_anim_notify_target = s_retry_anim_notify_process_target,
            .retry_logic_begin_play_target = s_retry_logic_begin_play_process_target,
            .create_retry_point_target = s_create_retry_point_process_event_target,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .protocol_name = s_bridge_protocol_name,
            .protocol_version = s_bridge_protocol_version,
            .build_stamp = s_build_stamp,
            .retry_anim_notify_path = s_retry_anim_notify_received_notify_path,
            .retry_logic_begin_play_path = s_retry_logic_actor_receive_begin_play_path,
            .retry_blueprint_latest_filename = s_runtime_kh3_retry_blueprint_event_filename,
            .retry_blueprint_events_filename = s_runtime_kh3_retry_blueprint_events_filename,
            .retry_point_latest_filename = s_runtime_kh3_retry_point_event_filename,
            .retry_point_events_filename = s_runtime_kh3_retry_point_events_filename,
        }};
    }

    auto RuntimeComposition::Impl::make_blocking_ui_hooks() -> RuntimeBlockingUiHookService
    {
        return RuntimeBlockingUiHookService{RuntimeBlockingUiHookConfig{
            .enabled = s_enable_runtime_blocking_ui_probe_hooks,
            .shoot_flow_restore_enabled = s_enable_he02_shoot_flow_restore,
            .runtime_state = s_runtime_state,
            .ap_message_gate_state = m_runtime_ap_message_gate_state,
            .ignored_map_active =
                [this]()
            {
                return m_player_reacquire.ignored_map_active();
            },
            .gameplay_controller_active =
                [this]()
            {
                return !m_native_hook_service.title_or_ui_controller_active();
            },
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .runtime_context_resolver =
                []()
            {
                return composition_context_summary();
            },
            .observe_pause_ui =
                [this](bool active)
            {
                m_he02_shoot_flow_restore.observe_pause_ui(active);
            },
            .build_stamp = s_build_stamp,
            .quiet_window = s_runtime_ap_message_quiet_window,
            .focus_handoff_grace = s_runtime_blocking_ui_focus_handoff_grace,
            .on_menu_closed =
                [this]()
            {
                s_runtime_state.grant_state()
                    .item_grant_poll_state()
                    .delivery_wake_schedule()
                    .request();
                mark_runtime_keyblade_menu_closed(s_runtime_state);
                m_periodic_bridge.request_keyblade_style_refresh();
                s_runtime_controller_abilities.request_refresh(
                    RuntimeControllerAbilityEvent::MenuClosed);
            },
            .before_map_load =
                []()
            {
                Kh02Story::release_dressing_room(false);
                character_swap_effects::notify_map_loading();
                character_style::reset();
                kairi_shining_hud::notify_map_loading();
                character_pawn_pool::clear();
                RuntimePortalService::on_map_jump();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_native_hook_service() -> RuntimeNativeHookService
    {
        return RuntimeNativeHookService{RuntimeNativeHookConfig{
            .runtime_state = s_runtime_state,
            .fz02_player_start = m_fz02_player_start,
            .tutorial_skips = m_tutorial_skips,
            .player_context = m_player_context,
            .battle_level_sync = m_battle_level_sync,
            .cutscene_skips = m_cutscene_skips,
            .story_progress = m_story_progress,
            .moogle_shop = m_moogle_shop,
            .sora_monitor = m_sora_level_monitor,
            .update_frame_context =
                [this](UFunction* frame, std::wstring_view hook)
            {
                return update_composition_frame_context(
                    frame, hook, m_goa_settings.auto_equip_movement_abilities());
            },
            .runtime_context_resolver =
                []()
            {
                return composition_context_summary();
            },
            .enter_ignored_map =
                [this](std::wstring_view context, std::wstring_view source)
            {
                request_music_update(MusicUpdate::Context);
                m_player_reacquire.enter_ignored_map(context, source);
            },
            .ignored_map_active =
                [this]()
            {
                return m_player_reacquire.ignored_map_active();
            },
            .reset_map_actor_lifecycle =
                [this]()
            {
                reset_runtime_map_actor_spawn_lifecycle_state();
            },
            .reset_map_lifecycle =
                [this]()
            {
                reset_runtime_map_lifecycle_state();
            },
            .fresh_sora_context_ready =
                [this](std::wstring_view reason)
            {
                request_music_update(MusicUpdate::Context);
                m_party_members.observe_sora_context_ready();
                // Post-possession: save state and controller
                // ownership are available before the area
                // banner finishes. Prepare world effects here.
                m_world_bootstrap.world_ready();
                m_quality_of_life.request_refresh();
                m_story_progress.notify_context_ready();
                m_workshop.request_refresh();
                m_periodic_bridge.notify_treasure_changed();
                m_map_actor_lifecycle.notify_context_ready();
                m_periodic_bridge.request_keyblade_style_refresh();
                m_periodic_bridge.request_lifecycle_refresh();
                RuntimePortalService::notify_context_ready();
                m_memory_hooks.reapply_lucky_mark_native_ap_retirements(reason);
            },
            .arm_controller_reacquire =
                [this](UObject* controller, std::wstring_view source)
            {
                m_player_reacquire.arm(controller, source);
            },
            .suppress_controller_reacquire =
                [this]()
            {
                m_player_reacquire.suppress();
            },
            .goa_party_map_ready =
                [this]()
            {
                request_music_update(MusicUpdate::Context);
                m_party_members.observe_goa_map_ready();
                RuntimePortalService::notify_context_ready();
            },
            .color_map_jump =
                [this]()
            {
                request_music_update(MusicUpdate::Context);
                m_colors.on_map_jump();
            },
            .portal_map_jump =
                [this]()
            {
                if (m_service_graph_ready)
                {
                    m_portals.on_map_jump();
                }
            },
        }};
    }

    auto RuntimeComposition::Impl::make_native_hook_lifecycle() -> RuntimeNativeHookLifecycleService
    {
        return RuntimeNativeHookLifecycleService{RuntimeNativeHookLifecycleConfig{
            .native_hooks = m_native_hook_service,
            .object_observations = m_runtime_object_observations,
            .game_thread_driver = m_runtime_game_thread_driver,
            .retry_probes = m_retry_probes,
            .expanded_save_slots_patch = m_expanded_save_slots_patch,
            .signed_munny_patch = m_signed_munny_patch,
            .sqex_translucent_slice_view_guard_patch = m_sqex_translucent_slice_view_guard_patch,
            .autosave_probe = m_autosave_probe,
            .blocking_ui_hooks = m_blocking_ui_hooks,
            .controller_abilities = s_runtime_controller_abilities,
            .sora_state_hooks = m_sora_state_hooks,
            .minimap_context = m_minimap_context,
            .hook_install_enabled = s_enable_native_hook_install,
            .expanded_save_slots_patch_enabled = s_enable_expanded_save_slots_patch,
            .signed_munny_patch_enabled = s_enable_signed_munny_patch,
            .sqex_translucent_slice_view_guard_patch_enabled =
                s_enable_sqex_translucent_slice_view_guard_patch,
            .game_engine_tick_hook_enabled = s_enable_game_engine_tick_detour,
            .runtime_object_observer_enabled = s_enable_runtime_uobject_create_observer,
            .autosave_probe_enabled = s_enable_runtime_save_tracking,
            .steam_save_startup_guard_enabled = s_enable_kh3_steam_save_startup_guard,
            .build_stamp = s_build_stamp,
            .protocol_name = s_bridge_protocol_name,
            .protocol_version = s_bridge_protocol_version,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .runtime_context_resolver =
                []()
            {
                return composition_context_summary();
            },
            .manual_save_completed_handler =
                [this](int32_t slot_number)
            {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                m_autoload.manual_save_completed(slot_number);
#endif
                m_goa_autosave.queue_after_manual_save(
                    std::format(STR("KHIII_slot{}.bin"), slot_number), slot_number);
            },
            .olympus_coliseum_save_completed_handler =
                [this](const std::filesystem::path& save_path)
            {
                if (m_service_graph_ready)
                {
                    m_olympus_coliseum_completion.process_progress_save(save_path);
                }
            },
            .profile_native_dispatch_install =
                [](std::function<void()> install)
            {
                const RuntimeProfileScope<s_enable_runtime_profiling> profile{STR("install_hooks")};
                install();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_runtime_startup() -> RuntimeStartupService
    {
        return RuntimeStartupService{RuntimeStartupConfig{
            s_runtime_state,
            m_runtime_game_thread_driver,
            m_native_hook_lifecycle,
            s_build_stamp,
            [this]()
            {
                m_kh02_runtime.initialize();
            },
        }};
    }
} // namespace kh3ap
