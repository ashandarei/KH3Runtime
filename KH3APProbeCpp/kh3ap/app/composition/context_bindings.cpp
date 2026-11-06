#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/hooks/hook_filters.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_helpers.h"
#include "kh3ap/diagnostics/retry/retry_blueprint_probe_state.h"
#include "kh3ap/diagnostics/retry/retry_point_probe_state.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap::composition_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace settings;
    const bool s_runtime_enum_resolver_configured = []()
    {
        configure_runtime_enum_resolver(
            std::filesystem::path{s_shared_tres_game_enums_relative_path});
        return true;
    }();
    const bool s_runtime_hook_filters_configured = []()
    {
        kh3ap::RuntimeHookFilterConfig config{};
        config.enable_runtime_reward_diagnostics = s_enable_runtime_reward_diagnostics;
        config.enable_bonus_trace_logging = s_enable_bonus_trace_logging;
        config.enable_stable_ability_trace_hooks = s_enable_stable_ability_trace_hooks;
        config.utility_reward_frame_names = {
            std::wstring{s_bp_get_vbonus_name},
        };
        config.diagnostic_hook_names = {
            std::wstring{s_bp_get_vbonus_name},
            std::wstring{s_wait_bp_get_vbonus_name},
            std::wstring{s_wait_bp_get_keyitem_name},
            std::wstring{s_wait_bp_get_multiitem_name},
            std::wstring{s_tres_get_vbonus_name},
            std::wstring{s_bp_ability_add_name},
            std::wstring{s_bp_ability_remove_name},
            std::wstring{s_bp_ability_get_level_name},
            std::wstring{s_bp_ability_get_equip_num_name},
            std::wstring{s_bp_is_ability_enable_name},
            std::wstring{s_bp_get_prize_name},
            std::wstring{s_bpev_get_item_name},
            std::wstring{s_apply_map_name_name},
            std::wstring{s_tres_show_map_name_name},
            std::wstring{s_tres_map_jump_name},
            std::wstring{s_bp_unlock_battle_portal_reward_action_name},
            std::wstring{s_remy_final_milestone_reward_name},
            std::wstring{s_tresui_show_get_treasure_prize_snow_curling_name},
            std::wstring{s_tresui_show_get_bonus_point_snow_curling_name},
            std::wstring{s_vehicle_get_troops_reward_data_name},
            std::wstring{s_treasure_box_anim_notify_name},
            std::wstring{s_ap_treasure_looted_name},
            std::wstring{s_treasure_logic_actor_get_treasure_name_name},
            std::wstring{s_treasure_logic_actor_get_smallest_vector_name},
            std::wstring{s_treasure_logic_actor_receive_begin_play_name},
            std::wstring{s_treasure_logic_actor_execute_ubergraph_name},
            std::wstring{s_tres_treasure_box_open_optional_name},
            std::wstring{s_tres_gimmick_actor_receive_do_command_name},
            std::wstring{s_convert_accessory_name},
            std::wstring{s_convert_protector_name},
            std::wstring{s_convert_weapon_name},
            std::wstring{s_convert_report_name},
            std::wstring{s_convert_keyitem_name},
            std::wstring{s_convert_battle_name},
            std::wstring{s_uip_map_name_on_callback_name},
            std::wstring{s_uip_multi_item_get_on_callback_name},
        };
        config.post_load_probe_hook_names = {
            std::wstring{s_apply_map_name_name},
            std::wstring{s_tres_show_map_name_name},
            std::wstring{s_tres_map_jump_name},
            std::wstring{s_uip_map_name_on_callback_name},
        };
        config.bonus_trace_hook_names = {
            std::wstring{s_bp_ability_add_name},
            std::wstring{s_bp_ability_remove_name},
            std::wstring{s_bp_get_vbonus_name},
            std::wstring{s_tres_get_vbonus_name},
        };
        config.stable_ability_trace_hook_names = {
            std::wstring{s_bp_ability_add_name},
            std::wstring{s_bp_ability_remove_name},
            std::wstring{s_bp_ability_get_level_name},
            std::wstring{s_bp_ability_get_equip_num_name},
            std::wstring{s_bp_is_ability_enable_name},
        };
        config.context_object_reward_state_hook_names = {
            std::wstring{s_uip_map_name_on_callback_name},
            std::wstring{s_uip_multi_item_get_on_callback_name},
            std::wstring{s_treasure_box_anim_notify_name},
            std::wstring{s_ap_treasure_looted_name},
            std::wstring{s_treasure_logic_actor_get_treasure_name_name},
            std::wstring{s_treasure_logic_actor_get_smallest_vector_name},
            std::wstring{s_treasure_logic_actor_receive_begin_play_name},
            std::wstring{s_treasure_logic_actor_execute_ubergraph_name},
            std::wstring{s_tres_treasure_box_open_optional_name},
            std::wstring{s_tres_gimmick_actor_receive_do_command_name},
        };
        config.kh3_load_lifecycle_hook_names = {
            std::wstring{s_tres_notify_pre_load_map_name},
            std::wstring{s_tres_notify_post_load_map_name},
            std::wstring{s_bp_notify_pre_load_map_name},
            std::wstring{s_bp_notify_post_load_map_name},
            std::wstring{s_create_auto_save_data_name},
            std::wstring{s_create_auto_save_data_ex_name},
            std::wstring{s_create_retry_point_name},
            std::wstring{s_discard_retry_point_name},
        };
        configure_runtime_hook_filters(std::move(config));
        return true;
    }();
    kh3ap::RuntimeState s_runtime_state{};
    kh3ap::RuntimeControllerAbilityService s_runtime_controller_abilities{
        kh3ap::RuntimeControllerAbilityConfig{
            .runtime_state = s_runtime_state,
            .wall_run_setter_path = s_tres_player_controller_base_bp_set_ability_wallrun_path,
            .style_change_setter_path =
                s_tres_player_controller_base_bp_set_ability_style_change_path,
            .magic_setter_path = s_tres_player_controller_base_bp_set_ability_magic_path,
            .attraction_flow_setter_path =
                s_tres_player_controller_base_bp_set_ability_attraction_flow_path,
            .airstep_setter_path = s_tres_player_controller_base_bp_set_ability_athletic_flow_path,
            .shoot_flow_setter_path =
                STR("/Script/TresGame.TresPlayerControllerBase:BP_SetAbilityShootFlow"),
            .style_change_mode =
                []()
            {
                return style_change_mode_from_seed();
            },
            .debug_logging = s_debug_logging,
        }};
    auto read_runtime_context_snapshot() -> std::optional<RuntimeContextSnapshot>
    {
        return kh3ap::read_runtime_context_snapshot(try_get_bridge_dir());
    }

    auto get_current_frame_context_snapshot() -> std::optional<RuntimeContextSnapshot>
    {
        return s_runtime_state.context_subsystem_state().context_state().current_snapshot();
    }

    auto composition_context_summary() -> std::wstring
    {
        // Prefer the in-memory snapshot refreshed by `update_current_frame_context`
        // on every hook call - the on-disk `runtime_context.json` can only be
        // written by Lua reward hooks that must stay disabled for stability.
        return kh3ap::runtime_context_summary(s_runtime_state, try_get_bridge_dir());
    }

    auto composition_context_object_is_not_real() -> bool
    {
        return kh3ap::runtime_context_object_is_not_real(s_runtime_state, try_get_bridge_dir());
    }

    auto is_vbonus_probe_hook_name(std::wstring_view hook_name) -> bool
    {
        return kh3ap::is_vbonus_probe_hook_name(hook_name, s_bp_get_vbonus_name,
                                                s_tres_get_vbonus_name, s_wait_bp_get_vbonus_name);
    }

    auto should_skip_vbonus_probe_for_runtime_context() -> bool
    {
        return kh3ap::should_skip_vbonus_probe_for_runtime_context(
            get_current_frame_context_snapshot());
    }

    auto maybe_publish_cached_pawn_runtime_context(std::chrono::steady_clock::time_point now,
                                                   std::wstring_view pawn_source,
                                                   bool force_publish) -> void
    {
        if (s_runtime_state.grant_state().special_battle_mode_active())
        {
            return;
        }

        kh3ap::maybe_publish_playable_runtime_context_from_cached_pawn(
            s_runtime_state, try_get_bridge_dir(), now, pawn_source, force_publish);
    }

    auto publish_goa_runtime_context(UObject* context, std::wstring_view hook_name) -> void
    {
        kh3ap::publish_goa_runtime_context(context, hook_name, s_runtime_state,
                                           try_get_bridge_dir());
    }

    auto maybe_rearm_runtime_grant_for_ready_context(const RuntimeContextSnapshot& snapshot,
                                                     bool auto_equip_movement) -> void
    {
        const auto bridge_dir = try_get_bridge_dir();
        const auto priority_request_path =
            kh3ap::get_runtime_item_grant_path(bridge_dir, s_runtime_item_grant_priority_filename);
        const auto normal_request_path =
            kh3ap::get_runtime_item_grant_path(bridge_dir, s_runtime_item_grant_filename);
        const auto& request_path =
            priority_request_path.has_value() &&
                    kh3ap::path_points_to_regular_file(*priority_request_path)
                ? priority_request_path
                : normal_request_path;
        kh3ap::maybe_rearm_runtime_grant_for_ready_context(
            snapshot, request_path, s_runtime_item_grant_delay, s_runtime_state, bridge_dir,
            [auto_equip_movement]
            {
                const std::wstring runtime_item_grant_context_summary =
                    composition_context_summary();
                auto apply_deps = runtime_item_grant_apply_deps(runtime_item_grant_context_summary);
                apply_deps.auto_equip_movement_abilities = auto_equip_movement;
                kh3ap::process_pending_runtime_item_grant(kh3ap::make_runtime_item_grant_poll_deps(
                    s_runtime_state, try_get_bridge_dir(), s_runtime_item_grant_filename,
                    s_runtime_item_grant_priority_filename, s_runtime_item_apply_event_filename,
                    s_bridge_protocol_name, s_bridge_protocol_version,
                    runtime_item_grant_context_summary, s_runtime_item_grant_delay,
                    s_runtime_item_grant_slow_wait_log_delay,
                    composition_context_object_is_not_real(),
                    apply_deps,
                    runtime_item_grant_post_apply_observer_deps(
                        runtime_item_grant_context_summary)));
            });
    }

    auto update_composition_frame_context(UFunction* frame_node, std::wstring_view hook_name,
                                           bool auto_equip_movement)
        -> std::optional<RuntimeContextSnapshot>
    {
        const auto derived = kh3ap::update_current_frame_context(
            frame_node, hook_name, s_runtime_state, try_get_bridge_dir());
        if (derived.has_value())
        {
            maybe_rearm_runtime_grant_for_ready_context(*derived, auto_equip_movement);
        }
        return derived;
    }
} // namespace kh3ap::composition_detail
