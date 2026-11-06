#pragma once

#include <cstdint>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::string_view runtime_guardians_and_mickey_walk_skip_slot_option =
            "skip_guardians_and_mickey_walk";
    inline constexpr std::wstring_view runtime_guardians_of_light_skip_flag =
            L"gameflow_KG_DLC";
    inline constexpr std::wstring_view runtime_guardians_of_light_skip_map_path =
            L"/Game/Levels/bt_DLC/bt_07/";
    inline constexpr int32_t runtime_guardians_of_light_skip_target = 21400;
    inline constexpr std::wstring_view runtime_guardians_of_light_skip_target_label =
            L"KG_DLC_07_BATTLE_01";

    struct RuntimeGuardiansOfLightSkipEntryState
    {
        bool applied_for_current_entry{false};

        [[nodiscard]] constexpr auto should_attempt(bool target_map_active) noexcept -> bool
        {
            if (!target_map_active)
            {
                applied_for_current_entry = false;
                return false;
            }
            return !applied_for_current_entry;
        }

        constexpr auto record_success() noexcept -> void
        {
            applied_for_current_entry = true;
        }
    };

    [[nodiscard]] constexpr auto is_runtime_guardians_of_light_skip_map_context(
            std::wstring_view world,
            std::wstring_view outer,
            std::wstring_view object) noexcept -> bool
    {
        return world.find(runtime_guardians_of_light_skip_map_path) != std::wstring_view::npos ||
               outer.find(runtime_guardians_of_light_skip_map_path) != std::wstring_view::npos ||
               object.find(runtime_guardians_of_light_skip_map_path) != std::wstring_view::npos;
    }
}
