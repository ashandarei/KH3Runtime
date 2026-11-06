#pragma once
#include <array>
#include <string_view>

namespace kh3ap
{
    enum class Kh02DiagnosticAction
    {
        Invalid,
        UnknownFlow,
        UnknownWithin,
        UnknownCastleTrigger,
        FlowLoadAll,
        Capture,
        EyesAutoLod,
        EyesLod0,
        FlowButton,
        FlowMenu,
        FlowCapture,
        FlowCastleTriggerAirCross,
        FlowCastleTriggerExpanded,
        FlowCastleTriggerInspect,
        FlowCastleTriggerOriginal,
        FlowCastleTriggerProbe,
        FlowCastleTriggerStage,
        FlowCastleTriggerTouch,
        FlowCastleTriggerWalk,
        FlowChestBind,
        FlowChestPress,
        FlowChestProbe,
        FlowChestApproach,
        FlowChestWalk,
        FlowInspect,
        FlowInventory,
        FlowInventoryHealthFixture,
        FlowTestExperience,
        FlowLoadCamera,
        FlowLoadEnemy,
        FlowLoadGameflow,
        FlowLoadGimmick,
        FlowMovieControl,
        FlowMoviePrepare,
        FlowMoviePrepareStory,
        FlowMovieResume,
        FlowMovieStop,
        FlowPrepare,
        FlowReloadHost,
        FlowResumeGears,
        FlowReturnTitle,
        FlowStarsInspect,
        FlowStarsLook,
        FlowStarsLookAway,
        FlowTestCharacterAqua,
        FlowTestCharacterSora,
        FlowTestHighestPoint,
        FlowStart,
        FlowTargetVisibilityTest,
        Inspect,
        Load,
        PlaytestInspect,
        PlaytestReset,
        Skip,
        Start,
        Unload,
        WithinCancel,
        WithinCapture,
        WithinCaptureUi,
        WithinFace,
        WithinInspect,
        WithinInteract,
        WithinMenu,
        WithinReplay,
        WithinReplayDw020,
        WithinTestApproachCore,
        WithinTestAttack,
        WithinTestCharacterAqua,
        WithinTestJump,
        WithinTestLeaveIslandsBattle,
        WithinWalkDepthsExit,
        WithinPrepareForestExit,
        WithinWalkForestExit,
    };

    enum class Kh02DiagnosticGroup
    {
        Invalid,
        Flow,
        Mirror,
        Opening,
        Reset
    };

    struct Kh02DiagnosticCommand
    {
        Kh02DiagnosticAction action{Kh02DiagnosticAction::Invalid};
        Kh02DiagnosticGroup group{Kh02DiagnosticGroup::Invalid};
    };

    inline auto kh02_decode_diagnostic_action(std::wstring_view name) -> Kh02DiagnosticCommand
    {
        using Action = Kh02DiagnosticAction;
        using Group = Kh02DiagnosticGroup;

        struct Entry
        {
            std::wstring_view name;
            Action action;
            Group group;
        };

        static constexpr std::array entries{
            Entry{L"capture", Action::Capture, Group::Opening},
            Entry{L"eyes_auto_lod", Action::EyesAutoLod, Group::Opening},
            Entry{L"eyes_lod0", Action::EyesLod0, Group::Opening},
            Entry{L"flow_button", Action::FlowButton, Group::Flow},
            Entry{L"flow_menu", Action::FlowMenu, Group::Flow},
            Entry{L"flow_capture", Action::FlowCapture, Group::Flow},
            Entry{L"flow_castle_trigger_air_cross", Action::FlowCastleTriggerAirCross, Group::Flow},
            Entry{L"flow_castle_trigger_expanded", Action::FlowCastleTriggerExpanded, Group::Flow},
            Entry{L"flow_castle_trigger_inspect", Action::FlowCastleTriggerInspect, Group::Flow},
            Entry{L"flow_castle_trigger_original", Action::FlowCastleTriggerOriginal, Group::Flow},
            Entry{L"flow_castle_trigger_probe", Action::FlowCastleTriggerProbe, Group::Flow},
            Entry{L"flow_castle_trigger_stage", Action::FlowCastleTriggerStage, Group::Flow},
            Entry{L"flow_castle_trigger_touch", Action::FlowCastleTriggerTouch, Group::Flow},
            Entry{L"flow_castle_trigger_walk", Action::FlowCastleTriggerWalk, Group::Flow},
            Entry{L"flow_chest_bind", Action::FlowChestBind, Group::Flow},
            Entry{L"flow_chest_press", Action::FlowChestPress, Group::Flow},
            Entry{L"flow_chest_probe", Action::FlowChestProbe, Group::Flow},
            Entry{L"flow_chest_approach", Action::FlowChestApproach, Group::Flow},
            Entry{L"flow_chest_walk", Action::FlowChestWalk, Group::Flow},
            Entry{L"flow_inspect", Action::FlowInspect, Group::Flow},
            Entry{L"flow_inventory", Action::FlowInventory, Group::Flow},
            Entry{L"flow_inventory_health_fixture", Action::FlowInventoryHealthFixture, Group::Flow},
            Entry{L"flow_test_experience", Action::FlowTestExperience, Group::Flow},
            Entry{L"flow_load_camera", Action::FlowLoadCamera, Group::Flow},
            Entry{L"flow_load_enemy", Action::FlowLoadEnemy, Group::Flow},
            Entry{L"flow_load_gameflow", Action::FlowLoadGameflow, Group::Flow},
            Entry{L"flow_load_gimmick", Action::FlowLoadGimmick, Group::Flow},
            Entry{L"flow_movie_control", Action::FlowMovieControl, Group::Flow},
            Entry{L"flow_movie_prepare", Action::FlowMoviePrepare, Group::Flow},
            Entry{L"flow_movie_prepare_story", Action::FlowMoviePrepareStory, Group::Flow},
            Entry{L"flow_movie_resume", Action::FlowMovieResume, Group::Flow},
            Entry{L"flow_movie_stop", Action::FlowMovieStop, Group::Flow},
            Entry{L"flow_prepare", Action::FlowPrepare, Group::Flow},
            Entry{L"flow_reload_host", Action::FlowReloadHost, Group::Flow},
            Entry{L"flow_resume_gears", Action::FlowResumeGears, Group::Flow},
            Entry{L"flow_return_title", Action::FlowReturnTitle, Group::Flow},
            Entry{L"flow_stars_inspect", Action::FlowStarsInspect, Group::Flow},
            Entry{L"flow_stars_look", Action::FlowStarsLook, Group::Flow},
            Entry{L"flow_stars_look_away", Action::FlowStarsLookAway, Group::Flow},
            Entry{L"flow_test_character_aqua", Action::FlowTestCharacterAqua, Group::Flow},
            Entry{L"flow_test_character_sora", Action::FlowTestCharacterSora, Group::Flow},
            Entry{L"flow_test_highest_point", Action::FlowTestHighestPoint, Group::Flow},
            Entry{L"flow_start", Action::FlowStart, Group::Flow},
            Entry{L"flow_target_visibility_test", Action::FlowTargetVisibilityTest, Group::Flow},
            Entry{L"inspect", Action::Inspect, Group::Opening},
            Entry{L"load", Action::Load, Group::Opening},
            Entry{L"playtest_inspect", Action::PlaytestInspect, Group::Reset},
            Entry{L"playtest_reset", Action::PlaytestReset, Group::Reset},
            Entry{L"skip", Action::Skip, Group::Opening},
            Entry{L"start", Action::Start, Group::Opening},
            Entry{L"unload", Action::Unload, Group::Opening},
            Entry{L"within_cancel", Action::WithinCancel, Group::Mirror},
            Entry{L"within_capture", Action::WithinCapture, Group::Mirror},
            Entry{L"within_capture_ui", Action::WithinCaptureUi, Group::Mirror},
            Entry{L"within_face", Action::WithinFace, Group::Mirror},
            Entry{L"within_inspect", Action::WithinInspect, Group::Mirror},
            Entry{L"within_interact", Action::WithinInteract, Group::Mirror},
            Entry{L"within_menu", Action::WithinMenu, Group::Mirror},
            Entry{L"within_replay", Action::WithinReplay, Group::Mirror},
            Entry{L"within_replay_dw020", Action::WithinReplayDw020, Group::Mirror},
            Entry{L"within_test_approach_core", Action::WithinTestApproachCore, Group::Mirror},
            Entry{L"within_test_attack", Action::WithinTestAttack, Group::Mirror},
            Entry{L"within_test_character_aqua", Action::WithinTestCharacterAqua, Group::Mirror},
            Entry{L"within_test_jump", Action::WithinTestJump, Group::Mirror},
            Entry{L"within_test_leave_islands_battle", Action::WithinTestLeaveIslandsBattle, Group::Mirror},
            Entry{L"within_walk_depths_exit", Action::WithinWalkDepthsExit, Group::Mirror},
            Entry{L"within_prepare_forest_exit", Action::WithinPrepareForestExit, Group::Mirror},
            Entry{L"within_walk_forest_exit", Action::WithinWalkForestExit, Group::Mirror}};
        for (const auto& entry : entries)
        {
            if (entry.name == name)
            {
                return {entry.action, entry.group};
            }
        }
        // Preserve the diagnostic prefix contracts, including unknown-action
        // guards and the historical flow_load* request for all source streams.
        if (name.starts_with(L"flow_castle_trigger_"))
        {
            return {Action::UnknownCastleTrigger, Group::Flow};
        }
        if (name.starts_with(L"flow_load"))
        {
            return {Action::FlowLoadAll, Group::Flow};
        }
        if (name.starts_with(L"flow_"))
        {
            return {Action::UnknownFlow, Group::Flow};
        }
        if (name.starts_with(L"within_"))
        {
            return {Action::UnknownWithin, Group::Mirror};
        }
        return {};
    }

    inline auto kh02_diagnostic_is_castle_trigger(Kh02DiagnosticAction action) -> bool
    {
        switch (action)
        {
        case Kh02DiagnosticAction::FlowCastleTriggerAirCross:
        case Kh02DiagnosticAction::FlowCastleTriggerExpanded:
        case Kh02DiagnosticAction::FlowCastleTriggerInspect:
        case Kh02DiagnosticAction::FlowCastleTriggerOriginal:
        case Kh02DiagnosticAction::FlowCastleTriggerProbe:
        case Kh02DiagnosticAction::FlowCastleTriggerStage:
        case Kh02DiagnosticAction::FlowCastleTriggerTouch:
        case Kh02DiagnosticAction::FlowCastleTriggerWalk:
        case Kh02DiagnosticAction::UnknownCastleTrigger:
            return true;
        default:
            return false;
        }
    }

    inline auto kh02_diagnostic_is_flow_load(Kh02DiagnosticAction action) -> bool
    {
        switch (action)
        {
        case Kh02DiagnosticAction::FlowLoadCamera:
        case Kh02DiagnosticAction::FlowLoadEnemy:
        case Kh02DiagnosticAction::FlowLoadGameflow:
        case Kh02DiagnosticAction::FlowLoadGimmick:
        case Kh02DiagnosticAction::FlowLoadAll:
            return true;
        default:
            return false;
        }
    }

    inline auto kh02_diagnostic_is_movie(Kh02DiagnosticAction action) -> bool
    {
        switch (action)
        {
        case Kh02DiagnosticAction::FlowMovieControl:
        case Kh02DiagnosticAction::FlowMoviePrepare:
        case Kh02DiagnosticAction::FlowMoviePrepareStory:
        case Kh02DiagnosticAction::FlowMovieResume:
        case Kh02DiagnosticAction::FlowMovieStop:
            return true;
        default:
            return false;
        }
    }

    inline auto kh02_diagnostic_allows_mirror_flow(Kh02DiagnosticAction action) -> bool
    {
        using Action = Kh02DiagnosticAction;
        return action == Action::FlowInspect || action == Action::FlowMoviePrepareStory ||
               action == Action::FlowMovieResume || action == Action::FlowMovieStop;
    }
} // namespace kh3ap
