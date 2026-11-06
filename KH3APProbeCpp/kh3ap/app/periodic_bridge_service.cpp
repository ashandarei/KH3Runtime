#include "kh3ap/bridge/session/command_sources.h"
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/periodic_bridge_service.h"
#include "kh3ap/bridge/rewards/reconciliation.h"

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/request_file_watcher.h"
#include "kh3ap/features/grants/messages/ap_message_call_helpers.h"
#include "kh3ap/features/grants/messages/ap_message_gate.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/grants/events/event_state.h"
#include "kh3ap/hooks/hook_integrity_diagnostic.h"
#include "kh3ap/features/grants/delivery/item_grant_service.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/features/player/keyblades/keyblade_options_helpers.h"
#include "kh3ap/diagnostics/ability_probe/manual_equipment_probe_helpers.h"
#include "kh3ap/diagnostics/grants/manual_item_grant_poll_helpers.h"
#include "kh3ap/diagnostics/grants/manual_keyblade_enhancement_probe_helpers.h"
#include "kh3ap/features/player/objects/map_actor_lifecycle_service.h"
#include "kh3ap/features/world/portals/map_jump_command_helpers.h"
#include "kh3ap/features/moogle_shop/moogle_shop_service.h"
#include "kh3ap/features/player/objects/nearby_actor_scan_helpers.h"
#include "kh3ap/features/story/remind/remind_completion_service.h"
#include "kh3ap/features/story/save_points/save_menu_command_helpers.h"
#include "kh3ap/features/story/save_points/save_point_spawn_helpers.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/treasure/treasure_retry_queues.h"
#include "kh3ap/features/treasure/treasure_runtime_state.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/features/story/save_points/save_point_discovery_service.h"

#include <atomic>
#include <utility>

#include <UnrealDef.hpp>

namespace kh3ap
{
    namespace
    {
        constexpr auto manual_grant_armed_delay = std::chrono::milliseconds(750);
        constexpr auto manual_grant_fallback_delay = std::chrono::milliseconds(1500);
        constexpr auto manual_grant_fallback_interval = std::chrono::seconds(2);

        using ::kh3ap::settings::s_actor_get_location_path;
        using ::kh3ap::settings::s_background_drain_interval;
        using ::kh3ap::settings::s_bp_get_equip_weapon_id_path;
        using ::kh3ap::settings::s_bridge_protocol_name;
        using ::kh3ap::settings::s_bridge_protocol_version;
        using ::kh3ap::settings::s_build_stamp;
        using ::kh3ap::settings::s_create_save_game_object_path;
        using ::kh3ap::settings::s_debug_logging;
        using ::kh3ap::settings::s_enable_manual_ability_state_probe;
        using ::kh3ap::settings::s_enable_manual_equipment_probe;
        using ::kh3ap::settings::s_enable_manual_keyblade_enhancement_probe;
        using ::kh3ap::settings::s_enable_manual_runtime_item_grant;
        using ::kh3ap::settings::s_enable_moogle_shop_checks;
        using ::kh3ap::settings::s_enable_runtime_hook_integrity_diagnostic;
        using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;
        using ::kh3ap::settings::s_enable_runtime_item_grant_polling;
        using ::kh3ap::settings::s_enable_runtime_keyblade_style_sanity;
        using ::kh3ap::settings::s_enable_runtime_map_jump_command;
        using ::kh3ap::settings::s_enable_runtime_nearby_actor_scan_request;
        using ::kh3ap::settings::s_enable_runtime_save_menu_command;
        using ::kh3ap::settings::s_enable_runtime_save_point_lifetime_deep_samples;
        using ::kh3ap::settings::s_enable_runtime_save_point_spawn_request;
        using ::kh3ap::settings::s_gameplay_statics_default_object_path;
        using ::kh3ap::settings::s_get_game_flag_boolean_path;
        using ::kh3ap::settings::s_get_game_flag_path;
        using ::kh3ap::settings::s_goa_save_point_data_blueprint_path;
        using ::kh3ap::settings::s_goa_save_point_data_class_path;
        using ::kh3ap::settings::s_kismet_print_string_path;
        using ::kh3ap::settings::s_kismet_system_library_default_object_path;
        using ::kh3ap::settings::s_load_game_from_slot_path;
        using ::kh3ap::settings::s_manual_ability_state_probe_filename;
        using ::kh3ap::settings::s_manual_ability_state_probe_result_filename;
        using ::kh3ap::settings::s_manual_equipment_probe_filename;
        using ::kh3ap::settings::s_manual_equipment_probe_result_filename;
        using ::kh3ap::settings::s_manual_keyblade_enhancement_probe_events_filename;
        using ::kh3ap::settings::s_manual_keyblade_enhancement_probe_filename;
        using ::kh3ap::settings::s_manual_keyblade_enhancement_probe_result_filename;
        using ::kh3ap::settings::s_manual_runtime_item_grant_filename;
        using ::kh3ap::settings::s_manual_runtime_item_grant_result_filename;
        using ::kh3ap::settings::s_runtime_ap_message_filename;
        using ::kh3ap::settings::s_runtime_ap_message_result_filename;
        using ::kh3ap::settings::s_runtime_file_poll_interval;
        using ::kh3ap::settings::s_runtime_hook_integrity_diagnostic_events_filename;
        using ::kh3ap::settings::s_runtime_hook_integrity_diagnostic_filename;
        using ::kh3ap::settings::s_runtime_hook_integrity_diagnostic_result_filename;
        using ::kh3ap::settings::s_runtime_item_grant_filename;
        using ::kh3ap::settings::s_runtime_item_grant_priority_filename;
        using ::kh3ap::settings::s_runtime_map_jump_events_filename;
        using ::kh3ap::settings::s_runtime_map_jump_request_filename;
        using ::kh3ap::settings::s_runtime_map_jump_result_filename;
        using ::kh3ap::settings::s_runtime_nearby_actor_scan_events_filename;
        using ::kh3ap::settings::s_runtime_nearby_actor_scan_request_filename;
        using ::kh3ap::settings::s_runtime_nearby_actor_scan_result_filename;
        using ::kh3ap::settings::s_runtime_portal_spawn_request_filename;
        using ::kh3ap::settings::s_runtime_save_menu_events_filename;
        using ::kh3ap::settings::s_runtime_save_menu_request_filename;
        using ::kh3ap::settings::s_runtime_save_menu_result_filename;
        using ::kh3ap::settings::s_runtime_save_point_default_my_map_marker_path;
        using ::kh3ap::settings::s_runtime_save_point_default_my_reactor_path;
        using ::kh3ap::settings::s_runtime_save_point_default_my_recover_path;
        using ::kh3ap::settings::s_runtime_save_point_default_player_start_tag;
        using ::kh3ap::settings::s_runtime_save_point_spawn_events_filename;
        using ::kh3ap::settings::s_runtime_save_point_spawn_request_filename;
        using ::kh3ap::settings::s_runtime_save_point_spawn_result_filename;
        using ::kh3ap::settings::s_save_game_to_slot_path;
        using ::kh3ap::settings::s_save_point_blueprint_path;
        using ::kh3ap::settings::s_save_point_class_path;
        using ::kh3ap::settings::s_set_flag_label_path;
        using ::kh3ap::settings::s_set_game_flag_boolean_path;
        using ::kh3ap::settings::s_set_game_flag_path;
        using ::kh3ap::settings::s_tres_enemy_pawn_e_ex_301_class_path;
        using ::kh3ap::settings::s_tres_enemy_pawn_e_ex_301_is_ascension_mode_path;
        using ::kh3ap::settings::s_tres_game_blueprint_library_default_object_path;
        using ::kh3ap::settings::s_tres_item_manager_path;
        using ::kh3ap::settings::s_tres_item_synthesis_checker_default_object_path;
        using ::kh3ap::settings::s_tres_item_weapon_enhance_data_path;
        using ::kh3ap::settings::s_tres_item_weapon_enhance_test_path;
        using ::kh3ap::settings::s_tres_map_jump_path;
        using ::kh3ap::settings::s_tres_player_controller_base_bp_notify_set_action_command1_path;
        using ::kh3ap::settings::s_tres_player_controller_base_bp_notify_start_special_anti_form_path;
        using ::kh3ap::settings::s_tres_player_controller_base_bp_set_ability_style_change_path;
        using ::kh3ap::settings::s_tres_player_controller_base_bp_set_restrict_style_path;
        using ::kh3ap::settings::s_tres_player_controller_base_notify_start_action_command_path;
        using ::kh3ap::settings::s_tres_player_controller_base_simulate_keypress_path;
        using ::kh3ap::settings::s_tres_reactor_do_command_delegate_signature_path;
        using ::kh3ap::settings::s_tresui_clear_information_text_cache_path;
        using ::kh3ap::settings::s_tresui_open_information_path;
        using ::kh3ap::settings::s_world_map_progress_flag;

        auto make_save_point_deps(
                RuntimeState& runtime_state,
                const std::optional<std::filesystem::path>& bridge_dir)
                -> RuntimeSavePointSpawnDeps
        {
            return RuntimeSavePointSpawnDeps{
                    runtime_state,
                    bridge_dir,
                    s_runtime_save_point_spawn_request_filename,
                    s_runtime_save_point_spawn_result_filename,
                    s_runtime_save_point_spawn_events_filename,
                    s_bridge_protocol_name,
                    s_bridge_protocol_version,
                    s_build_stamp,
                    s_save_point_class_path,
                    s_save_point_blueprint_path,
                    s_actor_get_location_path,
                    s_gameplay_statics_default_object_path,
                    s_load_game_from_slot_path,
                    s_create_save_game_object_path,
                    s_save_game_to_slot_path,
                    s_goa_save_point_data_class_path,
                    s_goa_save_point_data_blueprint_path,
                    s_runtime_save_point_default_player_start_tag,
                    s_runtime_save_point_default_my_reactor_path,
                    s_runtime_save_point_default_my_recover_path,
                    s_runtime_save_point_default_my_map_marker_path,
                    s_enable_runtime_save_point_lifetime_deep_samples,
                    s_debug_logging};
        }
    }

    struct RuntimePeriodicBridgeService::Impl
    {
        explicit Impl(RuntimePeriodicBridgeConfig config)
            : config(std::move(config))
        {
        }

        auto resolve_bridge_dir() const -> std::optional<std::filesystem::path>
        {
            return config.bridge_dir_resolver
                    ? config.bridge_dir_resolver()
                    : std::nullopt;
        }

        auto resolve_context() const -> std::wstring
        {
            return config.runtime_context_resolver
                    ? config.runtime_context_resolver()
                    : std::wstring{};
        }

        auto process_keyblade_style_sanity(
                std::chrono::steady_clock::time_point now) -> void
        {
            if constexpr (s_enable_runtime_keyblade_style_sanity)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:keyblade_style_sanity")};
                const auto bridge_dir = resolve_bridge_dir();
                const std::wstring context_summary = resolve_context();
                process_runtime_keyblade_style_sanity(
                        RuntimeKeybladeStyleSanityDeps{
                                config.runtime_state,
                                bridge_dir,
                                s_bp_get_equip_weapon_id_path,
                                s_tres_player_controller_base_bp_set_ability_style_change_path,
                                s_tres_player_controller_base_bp_set_restrict_style_path,
                                s_tres_player_controller_base_bp_notify_start_special_anti_form_path,
                                s_tres_enemy_pawn_e_ex_301_class_path,
                                s_tres_enemy_pawn_e_ex_301_is_ascension_mode_path,
                                context_summary,
                                now,
                                s_debug_logging});
            }
        }

        auto request_keyblade_style_refresh() -> void
        {
            keyblade_style_refresh_requested.store(
                    true,
                    std::memory_order_release);
        }

        static auto has_request(
                const std::optional<std::filesystem::path>& bridge_dir,
                std::wstring_view filename) -> bool
        {
            return bridge_dir.has_value() &&
                    path_points_to_regular_file(*bridge_dir / std::filesystem::path{filename});
        }

        auto process_runtime_file_poll(
                std::chrono::steady_clock::time_point now,
                const PhaseMarker& mark_phase,
                bool game_state_changed,
                bool diagnostic_commands) -> void
        {
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> poll_profile{STR("on_update:runtime_file_poll")};
            const auto bridge_dir = resolve_bridge_dir();
            const std::wstring context_summary = resolve_context();
            mark_phase(STR("runtime_file_poll_context"));

            if (game_state_changed || config.remind_completion.has_pending_work())
                config.remind_completion.process(bridge_dir, context_summary);
            mark_phase(STR("runtime_file_poll_remind_completion"));

            if constexpr (s_enable_moogle_shop_checks)
            {
                if (game_state_changed) config.moogle_shop.refresh(bridge_dir);
            }
            mark_phase(STR("runtime_file_poll_moogle_shop"));

            if constexpr (s_enable_manual_runtime_item_grant)
            {
                if (diagnostic_commands) {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:manual_runtime_item_grant")};
                const auto processor = [this](RC::Unreal::UObject* preferred_world_context) {
                    const std::wstring manual_context = resolve_context();
                    process_pending_manual_runtime_item_grant(
                            make_manual_runtime_item_grant_deps(
                                    config.runtime_state,
                                    resolve_bridge_dir(),
                                    s_manual_runtime_item_grant_filename,
                                    s_manual_runtime_item_grant_result_filename,
                                    s_bridge_protocol_name,
                                    s_bridge_protocol_version,
                                    config.item_grants.make_apply_deps(
                                            manual_context),
                                    config.live_ability_owner_probe,
                                    true),
                            preferred_world_context);
                };
                process_armed_manual_runtime_item_grant(
                        config.runtime_state,
                        manual_grant_armed_delay,
                        processor);
                process_pending_manual_runtime_item_grant_fallback(
                        config.runtime_state,
                        bridge_dir,
                        s_manual_runtime_item_grant_filename,
                        manual_grant_fallback_delay,
                        manual_grant_fallback_interval,
                        context_summary,
                        processor);
                }
            }
            mark_phase(STR("runtime_file_poll_manual_grant"));

            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{STR("on_update:ap_message")};
                const auto request_path = get_runtime_ap_message_path(
                        bridge_dir,
                        s_runtime_ap_message_filename);
                const bool request_exists = (request_path.has_value() &&
                        path_points_to_regular_file(*request_path)) || bool(reconciliation::view()->message);
                if (request_exists) {
                const std::wstring defer_reason = runtime_ap_message_defer_reason(
                        config.ap_message_gate_state,
                        config.runtime_state,
                        request_exists);
                if (!defer_reason.empty())
                {
                    maybe_log_runtime_ap_message_deferred(
                            config.ap_message_gate_state,
                            bridge_dir,
                            defer_reason,
                            s_build_stamp,
                            context_summary);
                }
                else
                {
                    process_pending_runtime_ap_message(
                            config.runtime_state,
                            bridge_dir,
                            request_path,
                            request_exists,
                            s_runtime_ap_message_result_filename,
                            s_bridge_protocol_name,
                            s_bridge_protocol_version,
                            s_tresui_open_information_path,
                            s_tresui_clear_information_text_cache_path,
                            s_tres_game_blueprint_library_default_object_path,
                            s_kismet_print_string_path,
                            s_kismet_system_library_default_object_path,
                            context_summary);
                }
                }
            }
            mark_phase(STR("runtime_file_poll_ap_message"));

            if constexpr (s_enable_runtime_hook_integrity_diagnostic)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:hook_integrity_diagnostic")};
                if (diagnostic_commands) process_pending_runtime_hook_integrity_diagnostic(
                        RuntimeHookIntegrityDiagnosticDeps{
                                bridge_dir,
                                s_runtime_hook_integrity_diagnostic_filename,
                                s_runtime_hook_integrity_diagnostic_result_filename,
                                s_runtime_hook_integrity_diagnostic_events_filename,
                                s_bridge_protocol_name,
                                s_bridge_protocol_version,
                                s_build_stamp,
                                context_summary});
            }
            mark_phase(STR("runtime_file_poll_hook_integrity"));

            if constexpr (s_enable_runtime_save_point_spawn_request)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:runtime_save_point_spawn")};
                // Deep diagnostic watches may need service without a new
                // command. Otherwise avoid setup and an unsuccessful file
                // open until a request exists; no absence is cached.
                if (diagnostic_commands && (s_enable_runtime_save_point_lifetime_deep_samples ||
                    has_request(bridge_dir, s_runtime_save_point_spawn_request_filename))
                   )
                {
                    process_pending_runtime_save_point_spawn(
                            make_save_point_deps(config.runtime_state, bridge_dir));
                }
            }
            mark_phase(STR("runtime_file_poll_save_point_request"));

            if constexpr (s_enable_runtime_save_menu_command)
            {
                for (const auto source : runtime_command_sources)
                {
                    const bool recovery = source == RuntimeCommandSource::Recovery;
                    if (!has_request(bridge_dir,recovery?channels::runtime_gameflow_command_file:s_runtime_save_menu_request_filename)) continue;
                    RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                            STR("on_update:runtime_save_menu_command")};
                    process_pending_runtime_save_menu_command(RuntimeSaveMenuCommandDeps{
                        config.runtime_state,
                        bridge_dir,
                        recovery ? channels::runtime_gameflow_command_file
                                 : s_runtime_save_menu_request_filename,
                        recovery ? channels::runtime_gameflow_result_file
                                 : s_runtime_save_menu_result_filename,
                        s_runtime_save_menu_events_filename,
                        s_bridge_protocol_name,
                        s_bridge_protocol_version,
                        s_build_stamp,
                        s_actor_get_location_path,
                        s_tres_reactor_do_command_delegate_signature_path,
                        s_tres_player_controller_base_bp_notify_set_action_command1_path,
                        s_tres_player_controller_base_notify_start_action_command_path,
                        s_tres_player_controller_base_simulate_keypress_path,
                        s_set_game_flag_path,
                        s_set_game_flag_boolean_path,
                        s_set_flag_label_path,
                        s_get_game_flag_path,
                        s_get_game_flag_boolean_path,
                        s_tres_game_blueprint_library_default_object_path,
                        s_world_map_progress_flag,
                        s_debug_logging});
                }
            }
            mark_phase(STR("runtime_file_poll_save_menu_command"));

            if constexpr (s_enable_runtime_map_jump_command)
            {
                for (const auto source : runtime_command_sources)
                {
                    const bool recovery = source == RuntimeCommandSource::Recovery;
                    if (!has_request(bridge_dir,recovery?channels::runtime_warp_command_file:s_runtime_map_jump_request_filename)) continue;
                    RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                            STR("on_update:runtime_map_jump_command")};
                    process_pending_runtime_map_jump_command(
                            RuntimeMapJumpCommandDeps{
                                    config.runtime_state,
                                    bridge_dir,
                                    recovery ? channels::runtime_warp_command_file : s_runtime_map_jump_request_filename,
                                    recovery ? channels::runtime_warp_result_file : s_runtime_map_jump_result_filename,
                                    s_runtime_map_jump_events_filename,
                                    s_bridge_protocol_name,
                                    s_bridge_protocol_version,
                                    s_build_stamp,
                                    s_tres_map_jump_path,
                                    s_tres_game_blueprint_library_default_object_path,
                                    s_debug_logging});
                }
            }
            mark_phase(STR("runtime_file_poll_map_jump_command"));

            if (diagnostic_commands) config.map_actor_lifecycle.process(bridge_dir, now, mark_phase);

            if constexpr (s_enable_runtime_nearby_actor_scan_request)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:runtime_nearby_actor_scan")};
                if (diagnostic_commands) process_pending_runtime_nearby_actor_scan(
                        RuntimeNearbyActorScanDeps{
                                config.runtime_state,
                                bridge_dir,
                                s_runtime_nearby_actor_scan_request_filename,
                                s_runtime_nearby_actor_scan_result_filename,
                                s_runtime_nearby_actor_scan_events_filename,
                                s_bridge_protocol_name,
                                s_bridge_protocol_version,
                                s_build_stamp,
                                s_actor_get_location_path,
                                s_debug_logging});
            }
            mark_phase(STR("runtime_file_poll_nearby_actor_scan"));

        }

        auto process_manual_probes() -> void
        {
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{STR("on_update:manual_probe_poll")};
            const auto bridge_dir = resolve_bridge_dir();
            std::optional<std::wstring> context;
            const auto pending_context = [&]() -> const std::wstring& {
                if (!context) context = resolve_context();
                return *context;
            };
            if constexpr (s_enable_manual_ability_state_probe)
            {
                if (has_request(bridge_dir, s_manual_ability_state_probe_filename))
                {
                    process_pending_manual_ability_state_probe(
                            make_manual_ability_state_probe_deps(
                                    config.runtime_state,
                                    bridge_dir,
                                    s_manual_ability_state_probe_filename,
                                    s_manual_ability_state_probe_result_filename,
                                    s_bridge_protocol_name,
                                    s_bridge_protocol_version,
                                    pending_context(),
                                    s_build_stamp,
                                    config.ability_array_probe,
                                    config.targeted_sora_state_probe,
                                    config.live_ability_owner_probe));
                }
            }
            if constexpr (s_enable_manual_equipment_probe)
            {
                if (has_request(bridge_dir, s_manual_equipment_probe_filename))
                {
                    process_pending_manual_equipment_probe(
                            make_manual_equipment_probe_deps(
                                    config.runtime_state,
                                    bridge_dir,
                                    s_manual_equipment_probe_filename,
                                    s_manual_equipment_probe_result_filename,
                                    s_bridge_protocol_name,
                                    s_bridge_protocol_version,
                                    pending_context(),
                                    s_build_stamp));
                }
            }
            if constexpr (s_enable_manual_keyblade_enhancement_probe)
            {
                if (has_request(bridge_dir, s_manual_keyblade_enhancement_probe_filename))
                {
                    process_pending_manual_keyblade_enhancement_probe(
                            make_manual_keyblade_enhancement_probe_deps(
                                    bridge_dir,
                                    s_manual_keyblade_enhancement_probe_filename,
                                    s_manual_keyblade_enhancement_probe_result_filename,
                                    s_manual_keyblade_enhancement_probe_events_filename,
                                    s_bridge_protocol_name,
                                    s_bridge_protocol_version,
                                    pending_context(),
                                    s_build_stamp,
                                    s_tres_item_weapon_enhance_data_path,
                                    s_tres_item_manager_path,
                                    s_tres_item_synthesis_checker_default_object_path,
                                    s_tres_item_weapon_enhance_test_path));
                }
            }
        }

        auto process_background_drain() -> void
        {
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{STR("on_update:background_drain")};
            const bool has_treasure_state_retries =
                    config.runtime_state.treasure_state()
                            .treasure_state_retry_queue()
                            .size() > 0;
            const bool has_treasure_logic_actor_retries =
                    config.runtime_state.treasure_state()
                            .treasure_logic_actor_retry_queue()
                            .size() > 0;
            const bool has_event_state_resamples =
                    config.runtime_state.event_state().deferred_resample_queue().size() > 0;
            std::optional<TreasureRuntimeDeps> treasure_runtime_deps{};
            if ((has_treasure_state_retries || has_treasure_logic_actor_retries) &&
                config.treasure_deps)
            {
                treasure_runtime_deps.emplace(config.treasure_deps());
            }
            if (has_treasure_state_retries && treasure_runtime_deps.has_value())
            {
                drain_due_open_treasure_state_retries(*treasure_runtime_deps);
            }
            if (has_treasure_logic_actor_retries && treasure_runtime_deps.has_value())
            {
                drain_due_treasure_logic_actor_retries(*treasure_runtime_deps);
            }
            if (has_event_state_resamples && config.event_snapshot_deps)
            {
                const auto deps = config.event_snapshot_deps();
                drain_due_deferred_event_state_resamples(deps);
            }
        }

        auto process_runtime_item_grant_poll() -> bool
        {
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                    STR("on_update:runtime_item_grant")};
            const auto bridge_dir = resolve_bridge_dir();
            const auto normal_request_path = get_runtime_item_grant_path(
                    bridge_dir,
                    s_runtime_item_grant_filename);
            const auto priority_request_path = get_runtime_item_grant_path(
                    bridge_dir,
                    s_runtime_item_grant_priority_filename);
            const bool request_pending =
                    (normal_request_path.has_value() &&
                     path_points_to_regular_file(*normal_request_path)) ||
                    (priority_request_path.has_value() &&
                     path_points_to_regular_file(*priority_request_path));
            if (!request_pending &&
                !has_request(bridge_dir, native_traps::request_file) && !native_traps::view()->head &&
                !reconciliation::view()->delivery &&
                config.runtime_state.grant_state().item_grant_poll_state().scheduled_queue().empty() &&
                config.runtime_state.grant_state().item_grant_poll_state().delivery_queue().empty() &&
                config.runtime_state.grant_state().item_grant_poll_state().delivery_queue(true).empty() &&
                !config.runtime_state.grant_state().item_grant_poll_state().is_armed())
            {
                return false;
            }

            const std::wstring context_summary = resolve_context();
            config.item_grants.process_pending(
                    context_summary,
                    bridge_dir,
                    runtime_context_object_is_not_real(context_summary));
            return true;
        }

        auto process(
                std::chrono::steady_clock::time_point now,
                bool runtime_file_polling_ready,
                const PhaseMarker& mark_phase) -> void
        {
            // Chest presentation must run during map startup, before the
            // general bridge file-poll startup delay and gameplay grant gates.
            // The service owns its bounded polling and UObject readiness checks.
            chest_persistence.process(config.runtime_state, resolve_bridge_dir(), now);
            mark_phase(STR("runtime_chest_persistence"));
            if (save_point_discovery.has_pending_work())
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{STR("on_update:save_point_discovery")};
                save_point_discovery.process(make_save_point_deps(config.runtime_state, resolve_bridge_dir()), now);
            }
            mark_phase(STR("runtime_save_point_discovery"));
            config.controller_abilities.process_pending_refresh(now);
            mark_phase(STR("controller_ability_refresh"));

            const bool style_event=keyblade_style_refresh_requested.exchange(false,std::memory_order_acquire);
            const auto& style=config.runtime_state.keyblade_style_state();
            const bool style_pending=style.force_apply || style.defer_apply_once || style.keyblade_save_pending ||
                style.ascension_actor_scan_requested || style.check_schedule.has_pending_settle();
            if (style_event || (style_pending && now>=next_style_retry &&
                config.runtime_state.grant_state().pawn_state().candidate_trusted()))
            {
                process_keyblade_style_sanity(now);
                next_style_retry=now+s_runtime_file_poll_interval;
            }
            mark_phase(STR("keyblade_style_refresh"));

            const bool pipe_updates = runtime_file_polling_ready && pipe::take_updates();

            if constexpr (s_enable_runtime_item_grant_polling)
            {
                // Request IPC and readiness hooks wake the game-thread consumer.
                // Only blocked/in-flight deliveries keep the retry watchdog.
                if (runtime_file_polling_ready)
                {
                    const bool request_changed = pipe::take_grant_updates();
                    auto& wake = config.runtime_state.grant_state().item_grant_poll_state().delivery_wake_schedule();
                    if (wake.should_process(now, request_changed))
                    {
                        wake.set_pending(process_runtime_item_grant_poll());
                    }
                }
            }
            mark_phase(STR("runtime_item_grant_poll"));

            bool diagnostic_commands=false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            {
                diagnostic_commands=runtime_file_polling_ready && diagnostic_requests.update(
                    resolve_bridge_dir(), now>=next_command_retry);
            }
#endif
            const bool lifecycle_refresh=runtime_file_polling_ready && lifecycle_refresh_requested.exchange(false);
            if (runtime_file_polling_ready && (pipe_updates || lifecycle_refresh || diagnostic_commands ||
                ((command_pending || bool(reconciliation::view()->message)) && now>=next_command_retry))) {
                const auto bridge_dir=resolve_bridge_dir();
                const auto revision=bridge_dir?pipe::input_revision(*bridge_dir/channels::game_state_file):0;
                const bool game_state_changed=lifecycle_refresh || bridge_dir!=last_bridge_dir || revision!=game_state_revision;
                last_bridge_dir=bridge_dir;game_state_revision=revision;
                command_pending=has_request(bridge_dir,channels::runtime_ap_message_file) || bool(reconciliation::view()->message) ||
                    has_request(bridge_dir,channels::runtime_gameflow_command_file) || has_request(bridge_dir,channels::runtime_warp_command_file) ||
                    config.remind_completion.has_pending_work();
                if (game_state_changed || diagnostic_commands || command_pending) {
                    process_runtime_file_poll(now, mark_phase,game_state_changed,diagnostic_commands);
                    command_pending=has_request(bridge_dir,channels::runtime_ap_message_file) || bool(reconciliation::view()->message) ||
                        has_request(bridge_dir,channels::runtime_gameflow_command_file) || has_request(bridge_dir,channels::runtime_warp_command_file) ||
                        config.remind_completion.has_pending_work();
                }
                next_command_retry=now+s_runtime_file_poll_interval;
            }
            if (runtime_file_polling_ready && config.map_actor_lifecycle.has_pending_work() && now>=next_map_retry) {
                config.map_actor_lifecycle.process(resolve_bridge_dir(),now,mark_phase);
                next_map_retry=now+s_runtime_file_poll_interval;
            }
            if (runtime_file_polling_ready && config.moogle_shop.has_pending_work() && now>=next_shop_retry) {
                config.moogle_shop.refresh(resolve_bridge_dir());
                next_shop_retry=now+s_runtime_file_poll_interval;
            }
            mark_phase(STR("runtime_file_poll"));

            if constexpr (s_enable_manual_ability_state_probe ||
                          s_enable_manual_equipment_probe ||
                          s_enable_manual_keyblade_enhancement_probe)
            {
                if (diagnostic_commands)
                {
                    process_manual_probes();
                }
            }
            mark_phase(STR("manual_probe_poll"));

            if (runtime_file_polling_ready &&
                should_run_periodic_update(
                        last_background_drain,
                        now,
                        s_background_drain_interval))
            {
                process_background_drain();
            }
            mark_phase(STR("background_drain"));
        }

        RuntimePeriodicBridgeConfig config;
        RuntimeChestPersistenceService chest_persistence{[this] {
            if(config.chest_completed)config.chest_completed();
        }};
        RuntimeSavePointDiscoveryService save_point_discovery;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RequestFileWatcher diagnostic_requests{
            s_manual_runtime_item_grant_filename,s_runtime_map_jump_request_filename,
            s_runtime_save_menu_request_filename,s_runtime_save_point_spawn_request_filename,
            s_runtime_portal_spawn_request_filename,s_runtime_nearby_actor_scan_request_filename,
            s_runtime_hook_integrity_diagnostic_filename,s_manual_ability_state_probe_filename,
            STR("runtime_frame_wait_request.json"),
            s_manual_equipment_probe_filename,s_manual_keyblade_enhancement_probe_filename};
#endif
        std::optional<std::filesystem::path> last_bridge_dir;
        uint64_t game_state_revision{};
        bool command_pending{};
        std::chrono::steady_clock::time_point next_command_retry{},next_map_retry{},next_shop_retry{},next_style_retry{};
        std::chrono::steady_clock::time_point last_background_drain{};
        std::atomic<bool> keyblade_style_refresh_requested{false};
        std::atomic<bool> lifecycle_refresh_requested{false};
    };

    RuntimePeriodicBridgeService::RuntimePeriodicBridgeService(
            RuntimePeriodicBridgeConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimePeriodicBridgeService::~RuntimePeriodicBridgeService() = default;
    RuntimePeriodicBridgeService::RuntimePeriodicBridgeService(
            RuntimePeriodicBridgeService&&) noexcept = default;
    auto RuntimePeriodicBridgeService::operator=(
            RuntimePeriodicBridgeService&&) noexcept
            -> RuntimePeriodicBridgeService& = default;

    auto RuntimePeriodicBridgeService::process(
            std::chrono::steady_clock::time_point now,
            bool runtime_file_polling_ready,
            const PhaseMarker& mark_phase) -> void
    {
        impl_->process(now, runtime_file_polling_ready, mark_phase);
    }

    auto RuntimePeriodicBridgeService::install_event_hooks() -> void
    {
        impl_->save_point_discovery.install_once();
    }

    auto RuntimePeriodicBridgeService::request_keyblade_style_refresh() -> void
    {
        impl_->request_keyblade_style_refresh();
    }

    auto RuntimePeriodicBridgeService::request_lifecycle_refresh() -> void
    {
        impl_->lifecycle_refresh_requested.store(true);
    }

    auto RuntimePeriodicBridgeService::notify_treasure_changed() -> void
    {
        impl_->chest_persistence.notify_treasure_changed();
    }

}
