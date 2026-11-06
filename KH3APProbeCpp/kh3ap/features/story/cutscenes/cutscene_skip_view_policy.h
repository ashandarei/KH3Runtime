#pragma once

#include <cmath>
#include <string_view>

namespace kh3ap
{
    inline constexpr auto cutscene_full_fade_min = 0.99f;
    inline constexpr auto cutscene_full_fade_max = 1.01f;

    constexpr bool cutscene_skip_view_scene(std::wstring_view class_path)
    {
        return class_path.starts_with(L"/Game/Cutscene/Localization/") &&
               (class_path.ends_with(L"/mi/cutscene_mi01_mi352.cutscene_mi01_mi352_C") ||
                class_path.ends_with(L"/mi/cutscene_mi01_mi353.cutscene_mi01_mi353_C"));
    }

    struct CutsceneSkipViewReadiness
    {
        bool same_world{};
        bool source_retired{};
        bool control_ready{};
        bool loading_closed{};
        bool paused{};
        bool cinematic{};
    };

    constexpr bool cutscene_skip_view_ready(const CutsceneSkipViewReadiness& state)
    {
        return state.same_world && state.source_retired && state.control_ready &&
               state.loading_closed && !state.paused && !state.cinematic;
    }

    inline bool cutscene_skip_view_stuck(float amount, bool changing)
    {
        return std::isfinite(amount) && amount >= cutscene_full_fade_min &&
               amount <= cutscene_full_fade_max && !changing;
    }
} // namespace kh3ap
