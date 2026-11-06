#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    inline constexpr std::wstring_view runtime_remind_completion_bridge_key =
            L"VBonus:VBonus_DLC_015";
    inline constexpr std::wstring_view runtime_remind_completion_location_name =
            L"Defeat Armored Xehanort in Re:Mind";
    inline constexpr std::wstring_view runtime_remind_completion_flag =
            L"gameflow_BT_DLC";
    inline constexpr int32_t runtime_remind_completion_target = 430;
    inline constexpr std::wstring_view runtime_remind_completion_target_label =
            L"BT_DLC_08_EVENT_04";

    [[nodiscard]] inline auto has_runtime_remind_completion_check(
            const std::vector<std::wstring>& checked_locations) -> bool
    {
        return std::find(
                       checked_locations.begin(),
                       checked_locations.end(),
                       runtime_remind_completion_bridge_key) !=
               checked_locations.end();
    }

    [[nodiscard]] constexpr auto runtime_remind_completion_needs_reconcile(
            int32_t current_value) noexcept -> bool
    {
        return current_value < runtime_remind_completion_target;
    }

    struct RuntimeRemindCompletionReconcileState
    {
        bool check_observed{false};
        bool completed_for_current_lifecycle{false};

        auto observe_checked_locations(
                const std::vector<std::wstring>& checked_locations) -> void
        {
            check_observed =
                    has_runtime_remind_completion_check(checked_locations);
        }

        [[nodiscard]] constexpr auto should_attempt() const noexcept -> bool
        {
            return check_observed && !completed_for_current_lifecycle;
        }

        constexpr auto record_success() noexcept -> void
        {
            completed_for_current_lifecycle = true;
        }

        constexpr auto reset_for_runtime_lifecycle() noexcept -> void
        {
            check_observed = false;
            completed_for_current_lifecycle = false;
        }
    };
}
