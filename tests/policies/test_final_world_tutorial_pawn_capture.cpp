#include "kh3ap/features/tutorials/final_world_tutorial_pawn_capture.h"

#include <cassert>
#include <string>

int main()
{
    const std::wstring exact_ew01_caller{
            kh3ap::runtime_final_world_tutorial_pawn_capture_ew01_caller_path};
    const std::wstring full_ew01_name =
            std::wstring{kh3ap::runtime_final_world_tutorial_pawn_capture_function_prefix} +
            exact_ew01_caller;
    const std::wstring exact_ew04_caller{
            kh3ap::runtime_final_world_tutorial_pawn_capture_ew04_caller_path};
    const std::wstring full_ew04_name =
            std::wstring{kh3ap::runtime_final_world_tutorial_pawn_capture_function_prefix} +
            exact_ew04_caller;

    assert(kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            exact_ew01_caller));
    assert(kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            full_ew01_name));
    assert(kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            exact_ew04_caller));
    assert(kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            full_ew04_name));
    assert(!kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            full_ew01_name + L"_lookalike"));
    assert(!kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            full_ew04_name + L"_lookalike"));
    assert(!kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            L"prefix " + exact_ew01_caller));
    assert(!kh3ap::is_runtime_final_world_tutorial_pawn_capture_event_caller(
            L"Function /Game/Levels/ew_DLC/ew_03/umap/ew_03_gameflow.ew_03_gameflow_C:ExecuteUbergraph_ew_03_gameflow"));

    assert(kh3ap::is_runtime_final_world_tutorial_pawn_identity(
            L"p_ew001_Pawn_C /Game/Levels/ew_DLC/ew_04/ew_04.ew_04:PersistentLevel.p_ew001_Pawn_C_0",
            L"p_ew001_Pawn_C"));
    assert(!kh3ap::is_runtime_final_world_tutorial_pawn_identity(
            L"p_ex001_Pawn_C /Game/Levels/ew_DLC/ew_04/ew_04.ew_04:PersistentLevel.p_ex001_Pawn_C_0",
            L"p_ex001_Pawn_C"));
    assert(!kh3ap::is_runtime_final_world_tutorial_pawn_identity(
            L"TresPlayerControllerSora /Game/Levels/ew_DLC/ew_04/ew_04.ew_04:PersistentLevel.TresPlayerControllerSora_0",
            L"TresPlayerControllerSora"));

    assert(kh3ap::should_capture_runtime_final_world_tutorial_pawn_scope(
            true,
            true,
            true,
            2299));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn_scope(
            true,
            true,
            true,
            2300));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn_scope(
            true,
            false,
            true,
            2299));

    assert(kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            true,
            full_ew01_name,
            true,
            2299));
    assert(kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            true,
            full_ew04_name,
            true,
            -1));

    // The requested boundary is strictly less than 2300.
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            true,
            full_ew04_name,
            true,
            2300));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            true,
            full_ew04_name,
            true,
            2301));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            false,
            true,
            full_ew04_name,
            true,
            2299));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            false,
            full_ew04_name,
            true,
            2299));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            true,
            full_ew04_name,
            false,
            2299));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            true,
            L"Function /Game/Levels/ew_DLC/ew_04/umap/ew_04_gameflow.ew_04_gameflow_C:ExecuteUbergraph_ew_04_gameflow",
            true,
            2300));
    assert(!kh3ap::should_capture_runtime_final_world_tutorial_pawn(
            true,
            true,
            L"Function /Game/Levels/ew_DLC/ew_03/umap/ew_03_gameflow.ew_03_gameflow_C:ExecuteUbergraph_ew_03_gameflow",
            true,
            2299));
    return 0;
}
