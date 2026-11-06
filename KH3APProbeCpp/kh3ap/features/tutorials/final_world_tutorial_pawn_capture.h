#pragma once

#include "kh3ap/features/player/state/sora_pawn_catalog.h"

#include <cstdint>
#include <string_view>

namespace kh3ap
{
    // This capture is intentionally limited to the early ew_01 story event.
    // KH3 may execute that event through either the base ew_01 gameflow or the
    // Re Mind ew_04 implementation. Both caller paths are matched exactly.
    // The upper bound is exclusive: gameflow_EW == 2300 must not qualify.
    inline constexpr int32_t runtime_final_world_tutorial_pawn_capture_gameflow_exclusive_max = 2300;
    inline constexpr std::wstring_view runtime_final_world_tutorial_pawn_capture_ew01_caller_path =
            L"/Game/Levels/ew/ew_01/umap/ew_01_gameflow.ew_01_gameflow_C:ExecuteUbergraph_ew_01_gameflow";
    inline constexpr std::wstring_view runtime_final_world_tutorial_pawn_capture_ew04_caller_path =
            L"/Game/Levels/ew_DLC/ew_04/umap/ew_04_gameflow.ew_04_gameflow_C:ExecuteUbergraph_ew_04_gameflow";
    inline constexpr std::wstring_view runtime_final_world_tutorial_pawn_capture_function_prefix =
            L"Function ";

    inline auto is_runtime_final_world_tutorial_pawn_capture_exact_caller(
            std::wstring_view frame_name,
            std::wstring_view caller_path) noexcept -> bool
    {
        if (frame_name == caller_path)
        {
            return true;
        }

        const auto prefix = runtime_final_world_tutorial_pawn_capture_function_prefix;
        return frame_name.size() == prefix.size() + caller_path.size() &&
               frame_name.substr(0, prefix.size()) == prefix &&
               frame_name.substr(prefix.size()) == caller_path;
    }

    inline auto is_runtime_final_world_tutorial_pawn_capture_event_caller(
            std::wstring_view frame_name) noexcept -> bool
    {
        return is_runtime_final_world_tutorial_pawn_capture_exact_caller(
                       frame_name,
                       runtime_final_world_tutorial_pawn_capture_ew01_caller_path) ||
               is_runtime_final_world_tutorial_pawn_capture_exact_caller(
                       frame_name,
                       runtime_final_world_tutorial_pawn_capture_ew04_caller_path);
    }

    inline auto is_runtime_final_world_tutorial_pawn_identity(
            std::wstring_view pawn_name,
            std::wstring_view class_name) noexcept -> bool
    {
        const RuntimeSoraPawnCatalogEntry* entry = runtime_sora_pawn_catalog_match(class_name);
        if (!entry)
        {
            entry = runtime_sora_pawn_catalog_match(pawn_name);
        }
        return entry && entry->kind == RuntimeSoraPawnKind::FadedFinalWorld;
    }

    inline auto should_capture_runtime_final_world_tutorial_pawn_scope(
            bool tutorial_pawn_capture_armed,
            bool final_world_tutorial_bypasses_applied,
            bool gameflow_value_available,
            int32_t gameflow_ew) noexcept -> bool
    {
        return tutorial_pawn_capture_armed &&
               final_world_tutorial_bypasses_applied &&
               gameflow_value_available &&
               gameflow_ew <
                       runtime_final_world_tutorial_pawn_capture_gameflow_exclusive_max;
    }

    inline auto should_capture_runtime_final_world_tutorial_pawn(
            bool tutorial_pawn_capture_armed,
            bool final_world_tutorial_bypasses_applied,
            std::wstring_view frame_name,
            bool gameflow_value_available,
            int32_t gameflow_ew) noexcept -> bool
    {
        return should_capture_runtime_final_world_tutorial_pawn_scope(
                       tutorial_pawn_capture_armed,
                       final_world_tutorial_bypasses_applied,
                       gameflow_value_available,
                       gameflow_ew) &&
               is_runtime_final_world_tutorial_pawn_capture_event_caller(frame_name);
    }
}
