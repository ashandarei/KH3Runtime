#pragma once
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"
#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace kh3ap
{
    // Original source BeginPlay scenery, without cinematic replay or flag writes.
    template <class FlagValue>
    auto kh02_castle_scenery_plan(int checkpoint, bool backtrack, FlagValue flag_value)
        -> std::vector<std::wstring_view>
    {
        if (!backtrack || checkpoint < kh02_checkpoint::gear_scene_dw012)
        {
            return {};
        }
        const auto* command = checkpoint >= kh02_checkpoint::gear_scene_dw013 ? L"dw02_Revisit"
                              : checkpoint >= kh02_checkpoint::fourth_gear    ? L"dw02_GEAR_Get4"
                              : checkpoint >= kh02_checkpoint::third_gear     ? L"dw02_GEAR_Get3"
                              : checkpoint >= kh02_checkpoint::second_gear    ? L"dw02_GEAR_Get2"
                              : checkpoint >= kh02_checkpoint::first_gear     ? L"dw02_GEAR_Get1"
                                                                              : L"dw02_CS_12_End";
        std::vector<std::wstring_view> commands{command};
        if (checkpoint >= kh02_checkpoint::first_gear)
        {
            for (const auto& [flag, map_command] : {std::pair{L"gameflow_DW_TUTORIAL_12_END", L"dw02_Get_Flight"},
                                                    std::pair{L"gameflow_DW_TUTORIAL_13_END", L"dw02_Get_Shoot"},
                                                    std::pair{L"gameflow_DW_TUTORIAL_14_END", L"dw02_Get_Slide"},
                                                    std::pair{L"gameflow_DW_TUTORIAL_15_END", L"dw02_Get_Style"}})
            {
                if (checkpoint >= kh02_checkpoint::gear_scene_dw014 || flag_value(flag) != 0)
                {
                    commands.push_back(map_command);
                }
            }
        }
        return commands;
    }
} // namespace kh3ap
