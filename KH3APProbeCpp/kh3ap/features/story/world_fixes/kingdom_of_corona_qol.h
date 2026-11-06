#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::wstring_view runtime_corona_story_flag =
            L"gameflow_RA";
    inline constexpr std::wstring_view runtime_corona_powerwild_camera_flag =
            L"gameflow_RA_PWildCamera1";
    inline constexpr std::wstring_view runtime_corona_event_frame_marker =
            L"/Game/Levels/ra/ra_01/";
    inline constexpr std::wstring_view
            runtime_corona_rabbit_passage_gate_flag =
                    L"gameflow_RA_Rabbit";
    inline constexpr std::wstring_view
            runtime_corona_rabbit_passage_activation_label =
                    L"RA_RABBIT_FINISH";
    inline constexpr std::wstring_view
            runtime_corona_rabbit_passage_gate_frame =
                    L"Function /Game/Maps/ra/umap/ra_01/"
                    L"ra_01_AreaC_A.ra_01_AreaC_A_C:"
                    L"ExecuteUbergraph_ra_01_AreaC_A";
    inline constexpr std::wstring_view
            runtime_corona_rabbit_passage_gate_context_marker =
                    L"ra_01_AreaC_A_C /Game/Maps/ra/umap/ra_01/"
                    L"ra_01_AreaC_A.ra_01_AreaC_A:PersistentLevel."
                    L"ra_01_AreaC_A_C_";

    // Cooked ra_01_AreaC_A owns the two GreaterEqual_FlagLabel calls that gate
    // this passage: gameflow_RA >= RA_CASTLE_EVENT1 and, when that is false,
    // gameflow_RA_Rabbit >= RA_RABBIT_FINISH. Both true branches converge on
    // the same stock map-authored opening path, which removes SMC_ra_Box_18
    // and applies the intended bush visibility. Native post-hooks run after
    // the Blueprint VM has consumed the function arguments, so identify this
    // gate by its exact Level Blueprint callsite rather than rereading stale
    // parameter storage. Do not write either flag and do not patch actors.
    [[nodiscard]] constexpr auto
    is_runtime_corona_rabbit_passage_gate_context(
            std::wstring_view context_name) -> bool
    {
        if (context_name.size() <
                    runtime_corona_rabbit_passage_gate_context_marker.size() ||
            context_name.substr(
                    0,
                    runtime_corona_rabbit_passage_gate_context_marker.size()) !=
                    runtime_corona_rabbit_passage_gate_context_marker)
        {
            return false;
        }
        size_t offset =
                runtime_corona_rabbit_passage_gate_context_marker.size();
        const size_t digit_start = offset;
        while (offset < context_name.size() &&
               context_name[offset] >= L'0' && context_name[offset] <= L'9')
        {
            ++offset;
        }
        return offset > digit_start &&
               (offset == context_name.size() || context_name[offset] == L' ');
    }

    [[nodiscard]] constexpr auto
    is_runtime_corona_rabbit_passage_gate_callsite(
            std::wstring_view frame_name,
            std::wstring_view context_name) -> bool
    {
        return frame_name == runtime_corona_rabbit_passage_gate_frame ||
               is_runtime_corona_rabbit_passage_gate_context(context_name);
    }

    struct RuntimeKingdomOfCoronaStoryTransition
    {
        int32_t observed_value{};
        int32_t target_value{};
        std::wstring_view observed_label{};
        std::wstring_view target_label{};
        std::wstring_view companion_flag{};
        int32_t companion_value{};
        std::wstring_view companion_label{};
    };

    inline constexpr std::array<
            RuntimeKingdomOfCoronaStoryTransition,
            2>
            runtime_corona_story_transitions{{
                    {
                            420,
                            500,
                            L"RA_TOWER_EVENT2",
                            L"RA_FOREST_REAL10",
                            L"gameflow_RA_Dandelion",
                            10,
                            L"RA_DANDELION_START",
                    },
                    {
                            2031,
                            2500,
                            L"RA_FOREST_BATTLE21",
                            L"RA_FOREST_REAL30",
                            L"gameflow_RA_Bird",
                            10,
                            L"RA_BIRD_START",
                    },
            }};

    [[nodiscard]] constexpr auto find_runtime_corona_story_transition(
            std::wstring_view flag_name,
            int32_t observed_value)
            -> const RuntimeKingdomOfCoronaStoryTransition*
    {
        if (flag_name != runtime_corona_story_flag)
        {
            return nullptr;
        }
        for (const auto& transition : runtime_corona_story_transitions)
        {
            if (transition.observed_value == observed_value)
            {
                return &transition;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr auto find_runtime_corona_story_label_transition(
            std::wstring_view flag_name,
            std::wstring_view observed_label)
            -> const RuntimeKingdomOfCoronaStoryTransition*
    {
        if (flag_name != runtime_corona_story_flag)
        {
            return nullptr;
        }
        for (const auto& transition : runtime_corona_story_transitions)
        {
            if (transition.observed_label == observed_label)
            {
                return &transition;
            }
        }
        return nullptr;
    }

    [[nodiscard]] constexpr auto is_runtime_corona_powerwild_camera_flag(
            std::wstring_view flag_name) -> bool
    {
        return flag_name == runtime_corona_powerwild_camera_flag;
    }

    [[nodiscard]] constexpr auto is_runtime_corona_event_frame(
            std::wstring_view frame_name) -> bool
    {
        return frame_name.find(runtime_corona_event_frame_marker) !=
               std::wstring_view::npos;
    }

}
