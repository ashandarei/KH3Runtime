#pragma once

#include <array>
#include <string_view>

namespace kh3ap::space
{
    inline constexpr std::wstring_view eclipse_world = L"World /Game/Levels/gm/gm_03/gm_03.gm_03";
    inline constexpr int eclipse_final_camera = 200;
    inline constexpr int eclipse_boss_ready = 500;
    inline constexpr std::array eclipse_recoverable_stages{
        10,  // START
        100, // CAMERA_01
        110, // ARENA_02
        120, // ARENA_03
        eclipse_final_camera, // CAMERA_10
    };

    // These are native victories, not AP receipts or the order-sensitive camera
    // counter. Never manufacture a prerequisite, replay loot, or rewind a boss.
    constexpr bool recover_eclipse_unlock(bool access_ready, bool include_space,
        std::wstring_view world, const std::array<bool, 5>& victories, int stage, bool omega_defeated = false)
    {
        if (!access_ready || !include_space || world != eclipse_world || omega_defeated)
        {
            return false;
        }
        for (bool defeated : victories)
        {
            if (!defeated)
            {
                return false;
            }
        }
        // Shipped TresGameLabels: START, CAMERA_01, ARENA_02, ARENA_03,
        // CAMERA_10. INIT has not completed Colossus' story transition.
        for (const int recoverable_stage : eclipse_recoverable_stages)
        {
            if (stage == recoverable_stage)
            {
                return true;
            }
        }
        return false;
    }
}
