#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::string_view runtime_beat_of_lead_slot_option =
            "fast_beat_of_lead";
    inline constexpr auto runtime_beat_of_lead_retry_interval =
            std::chrono::milliseconds(250);

    inline constexpr std::wstring_view runtime_beat_of_lead_manager_class_name =
            L"g_ts_GigasGameMgr_C";
    inline constexpr std::wstring_view runtime_beat_of_lead_manager_package_fragment =
            L"/Game/Blueprints/Gimmick/ts/g_ts_GigasGameMgr.";
    inline constexpr std::wstring_view runtime_beat_of_lead_level_family_fragment =
            L"/Game/Levels/ts/ts_03/";
    inline constexpr std::wstring_view runtime_beat_of_lead_default_object_fragment =
            L"Default__g_ts_GigasGameMgr_C";
    inline constexpr std::wstring_view runtime_beat_of_lead_live_instance_fragment =
            L":PersistentLevel.g_ts_GigasGameMgr";
    inline constexpr std::wstring_view runtime_beat_of_lead_minigame_property_name =
            L"Minigame";
    inline constexpr std::wstring_view runtime_beat_of_lead_score_property_name =
            L"SumScore";
    inline constexpr std::wstring_view runtime_beat_of_lead_level_script_class_name =
            L"ts_03_gameflow_C";
    inline constexpr std::wstring_view runtime_beat_of_lead_level_script_package_fragment =
            L"/Game/Levels/ts/ts_03/umap/ts_03_gameflow.";
    inline constexpr std::wstring_view runtime_beat_of_lead_wipe_out_function_name =
            L"OnWipeOut";
    inline constexpr std::wstring_view runtime_beat_of_lead_wipe_out_group_param_name =
            L"GroupName";
    inline constexpr std::wstring_view runtime_beat_of_lead_wipe_out_enemy_param_name =
            L"Enemy";
    inline constexpr std::wstring_view runtime_beat_of_lead_wipe_out_killed_by_param_name =
            L"KilledBy";
    inline constexpr std::wstring_view runtime_beat_of_lead_story_wipe_out_group =
            L"hyper";

    enum class RuntimeBeatOfLeadObjectKind
    {
        NotTarget,
        ClassDefaultObject,
        LiveInstance,
    };

    [[nodiscard]] constexpr auto classify_runtime_beat_of_lead_object(
            std::wstring_view class_name,
            std::wstring_view full_name) noexcept -> RuntimeBeatOfLeadObjectKind
    {
        if (class_name != runtime_beat_of_lead_manager_class_name)
        {
            return RuntimeBeatOfLeadObjectKind::NotTarget;
        }
        if (full_name.find(runtime_beat_of_lead_manager_package_fragment) !=
                    std::wstring_view::npos &&
            full_name.find(runtime_beat_of_lead_default_object_fragment) !=
                    std::wstring_view::npos)
        {
            return RuntimeBeatOfLeadObjectKind::ClassDefaultObject;
        }
        if (full_name.find(runtime_beat_of_lead_level_family_fragment) !=
                    std::wstring_view::npos &&
            full_name.find(runtime_beat_of_lead_live_instance_fragment) !=
                    std::wstring_view::npos)
        {
            return RuntimeBeatOfLeadObjectKind::LiveInstance;
        }
        return RuntimeBeatOfLeadObjectKind::NotTarget;
    }

    enum class RuntimeBeatOfLeadCompletionAction
    {
        Ignored,
        Waiting,
        Complete,
    };

    struct RuntimeBeatOfLeadCompletionTracker
    {
        uint64_t manager_identity{};
        bool completion_dispatched{};

        constexpr auto observe_manager(uint64_t identity) noexcept -> bool
        {
            if (identity == 0 || identity == manager_identity)
            {
                return false;
            }
            manager_identity = identity;
            completion_dispatched = false;
            return true;
        }

        [[nodiscard]] constexpr auto observe_state(
                uint64_t identity,
                bool repeatable_minigame,
                int32_t score) noexcept -> RuntimeBeatOfLeadCompletionAction
        {
            observe_manager(identity);
            if (identity == 0)
            {
                return RuntimeBeatOfLeadCompletionAction::Ignored;
            }
            if (repeatable_minigame || score <= 0)
            {
                return RuntimeBeatOfLeadCompletionAction::Waiting;
            }
            if (completion_dispatched)
            {
                return RuntimeBeatOfLeadCompletionAction::Ignored;
            }

            completion_dispatched = true;
            return RuntimeBeatOfLeadCompletionAction::Complete;
        }

        constexpr auto rearm_after_failed_completion() noexcept -> void
        {
            completion_dispatched = false;
        }

        constexpr auto reset() noexcept -> void
        {
            manager_identity = 0;
            completion_dispatched = false;
        }
    };
}
