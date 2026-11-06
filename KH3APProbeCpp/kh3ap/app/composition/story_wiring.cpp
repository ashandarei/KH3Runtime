#include "kh3ap/app/composition/service_graph.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/features/story/cutscenes/cutscene_skip_service.h"
#include "kh3ap/features/story/minigames/beat_of_lead_service.h"
#include "kh3ap/features/story/world_fixes/festival_dance_story_service.h"
#include "kh3ap/features/story/world_fixes/fz02_player_start_service.h"
#include "kh3ap/features/player/emblems/lucky_emblem_tracker_service.h"
#include "kh3ap/features/story/remind/remind_completion_service.h"
#include "kh3ap/features/story/cutscenes/shoot_flow_restore_helpers.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/features/story/minigames/union_cross_command_service.h"
#include "kh3ap/features/story/quality_of_life_service.h"
#include "kh3ap/features/story/progression/story_progress_service.h"
#include "kh3ap/core/types.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    using namespace composition_detail;
    using namespace composition_detail::settings;
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        auto get_or_find_function_object(HookTargetInfo& hook_info) -> UFunction*
        {
            if (!hook_info.function_object)
            {
                hook_info.function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, hook_info.function_path);
            }
            return hook_info.function_object;
        }
    } // namespace
    auto RuntimeComposition::Impl::make_he02_shoot_flow_restore() -> RuntimeShootFlowRestoreService
    {
        return RuntimeShootFlowRestoreService{RuntimeShootFlowRestoreConfig{
            s_runtime_state,
            []()
            {
                return try_get_bridge_dir();
            },
            []()
            {
                return composition_context_summary();
            },
            s_build_stamp, s_tres_player_controller_base_bp_set_ability_shoot_flow_name,
            s_runtime_he02_shoot_flow_restore_event_filename,
            s_runtime_he02_shoot_flow_restore_events_filename,
            s_he02_shoot_flow_restore_settle_delay}};
    }

    auto RuntimeComposition::Impl::make_story_progress() -> RuntimeStoryProgressService
    {
        return RuntimeStoryProgressService{RuntimeStoryProgressConfig{
            s_runtime_state,
            []()
            {
                return get_current_frame_context_snapshot();
            },
            []()
            {
                return composition_context_summary();
            },
            []()
            {
                return guardians_and_mickey_walk_skip_enabled_from_seed();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_quality_of_life() -> RuntimeQualityOfLifeService
    {
        return RuntimeQualityOfLifeService{RuntimeQualityOfLifeConfig{
            .runtime_state = s_runtime_state,
            .bridge_dir_resolver =
                []()
            {
                return try_get_bridge_dir();
            },
            .get_game_flag_path = s_get_game_flag_path,
            .get_game_flag_boolean_path = s_get_game_flag_boolean_path,
            .set_game_flag_path = s_set_game_flag_path,
            .set_game_flag_boolean_path = s_set_game_flag_boolean_path,
            .blueprint_library_default_object_path =
                s_tres_game_blueprint_library_default_object_path,
            .enabled = s_enable_quality_of_life_actor_replacement,
            .debug_logging = s_debug_logging,
            .retry_interval = s_runtime_quality_of_life_retry_interval,
        }};
    }

    auto RuntimeComposition::Impl::make_fz02_player_start() -> RuntimeFz02PlayerStartService
    {
        return RuntimeFz02PlayerStartService{RuntimeFz02PlayerStartConfig{
            [](HookTargetInfo& hook_info)
            {
                return get_or_find_function_object(hook_info);
            },
        }};
    }

    auto RuntimeComposition::Impl::make_tutorial_skips() -> RuntimeTutorialSkipService
    {
        return RuntimeTutorialSkipService{RuntimeTutorialSkipConfig{
            s_enable_olympus_completion_tutorial_bypass,
            s_enable_olympus_early_tutorial_bypasses,
            s_enable_olympus_save_points_tutorial_bypass,
            s_enable_caribbean_sea_spectacle_tutorial_bypass,
            s_enable_toy_box_tutorial_bypasses,
            s_enable_twilight_town_tutorial_bypasses,
            s_enable_final_world_ui_bypasses,
            s_enable_link_tutorial_bypasses,
            s_enable_informational_help_bypasses,
            s_he01_level_path_prefix,
            s_he01_gameflow_execute_ubergraph_path,
            s_he02_level_path_prefix,
            s_he02_gameflow_execute_ubergraph_path,
            s_he03_level_path_prefix,
            s_he03_gameflow_execute_ubergraph_path,
            s_ca02_level_path_prefix,
            s_ca02_island01_treasure01_execute_ubergraph_path,
            {s_ts01_level_path_prefix, s_ts02_level_path_prefix, s_ts02_level_path_prefix,
             s_ts03_level_path_prefix},
            {s_ts01_gameflow_execute_ubergraph_path, s_ts02_camera_ms030e_execute_ubergraph_path,
             s_ts02_denjigimmickfront_execute_ubergraph_path,
             s_ts03_gameflow_execute_ubergraph_path},
            s_tt01_level_path_prefix,
            s_tt01_gameflow_execute_ubergraph_path,
            {s_ew04_gameflow_execute_ubergraph_path, s_ew01_gameflow_execute_ubergraph_path},
        }};
    }

    auto RuntimeComposition::Impl::make_cutscene_skips() -> RuntimeCutsceneSkipService
    {
        return RuntimeCutsceneSkipService{RuntimeCutsceneSkipConfig{
            []()
            {
                return try_get_bridge_dir();
            },
            []()
            {
                return composition_context_summary();
            },
            []()
            {
                return auto_skip_cutscenes_enabled_from_seed();
            },
            s_build_stamp,
            s_tres_level_script_actor_on_event_skip_path,
            s_enable_cutscene_skip_candidate_catalog,
            s_enable_cutscene_ui_probe_hooks,
            s_enable_cutscene_end_skip_probe_logging,
            s_enable_cutscene_state_probe_logging,
        }};
    }

    auto RuntimeComposition::Impl::make_remind_completion() -> RuntimeRemindCompletionService
    {
        return RuntimeRemindCompletionService{RuntimeRemindCompletionConfig{
            s_runtime_state,
            s_get_game_flag_path,
            s_set_game_flag_path,
            s_tres_game_blueprint_library_default_object_path,
            s_enable_remind_completion_check_reconcile,
            s_debug_logging,
        }};
    }

    auto RuntimeComposition::Impl::make_lucky_emblem_tracker() -> RuntimeLuckyEmblemTrackerService
    {
        return RuntimeLuckyEmblemTrackerService{RuntimeLuckyEmblemTrackerConfig{
            .enabled = s_enable_lucky_emblem_tracker,
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
            .playable_pawn_resolver =
                [this]() -> UObject*
            {
                return !m_player_context.map_ready_context_key().empty()
                    ? m_player_context.trusted_current_playable_cached_sora_pawn() : nullptr;
            },
            .build_stamp = s_build_stamp,
            .state_filename = s_runtime_lucky_emblem_state_filename,
            .recorded_function_name = s_hidden_mickey_is_lucky_mark_recorded_function_name,
            .mark_id_property_name = s_lucky_emblem_mark_id_property_name,
            .retry_interval = s_lucky_emblem_tracker_retry_interval,
        }};
    }

    auto RuntimeComposition::Impl::make_festival_dance_story() -> RuntimeFestivalDanceStoryService
    {
        return RuntimeFestivalDanceStoryService{RuntimeFestivalDanceStoryConfig{
            s_enable_festival_dance_story_fast_completion,
            []()
            {
                return fast_dance_minigame_enabled_from_seed();
            },
            [this]()
            {
                m_runtime_object_observations.disable_festival_dance_story_observation();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_beat_of_lead() -> RuntimeBeatOfLeadService
    {
        return RuntimeBeatOfLeadService{RuntimeBeatOfLeadConfig{
            s_enable_beat_of_lead_story_fast_completion,
            []()
            {
                return fast_beat_of_lead_enabled_from_seed();
            },
        }};
    }

    auto RuntimeComposition::Impl::make_union_cross_commands() -> RuntimeUnionCrossCommandService
    {
        return RuntimeUnionCrossCommandService{RuntimeUnionCrossCommandConfig{
            s_enable_union_cross_immediate_completion,
            []()
            {
                return fast_union_cross_enabled_from_seed();
            },
        }};
    }
} // namespace kh3ap
