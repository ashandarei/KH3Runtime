#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeFinalWorldGuidanceBypassResult
    {
        Patched,
        AlreadyPatched,
        Rejected,
    };

    // Both the base ew_01 gameflow and Re Mind's ew_04 transition gameflow
    // contain the same guidance choice. Their bytecode layouts differ, so
    // validate and support both exact layouts rather than assuming the visible
    // ew_01 map also owns the executing gameflow.
    inline constexpr size_t runtime_final_world_guidance_ew01_script_size = 127098;
    inline constexpr size_t runtime_final_world_guidance_ew01_gate_offset = 0x856C;
    inline constexpr size_t runtime_final_world_guidance_ew01_prompt_offset = 0x857A;
    inline constexpr size_t runtime_final_world_guidance_ew01_gate_false_offset = 0xA8E3;
    inline constexpr size_t runtime_final_world_guidance_ew01_darkside_end_jump_offset = 0x1EF4C;
    inline constexpr size_t runtime_final_world_guidance_ew01_choice_call_offset = 0x88B6;
    inline constexpr size_t runtime_final_world_guidance_ew01_choice_pop_offset = 0x893C;
    inline constexpr size_t runtime_final_world_guidance_ew01_third_choice_jump_offset = 0x89BF;
    inline constexpr size_t runtime_final_world_guidance_ew01_third_choice_branch_offset = 0x89CE;
    inline constexpr size_t runtime_final_world_guidance_ew01_message_pop_offset = 0x8B15;
    inline constexpr size_t runtime_final_world_guidance_ew01_resume_offset = 0x8B16;

    inline constexpr size_t runtime_final_world_guidance_ew04_script_size = 134274;
    inline constexpr size_t runtime_final_world_guidance_ew04_gate_offset = 0x888E;
    inline constexpr size_t runtime_final_world_guidance_ew04_prompt_offset = 0x889C;
    inline constexpr size_t runtime_final_world_guidance_ew04_gate_false_offset = 0xAA27;
    inline constexpr size_t runtime_final_world_guidance_ew04_darkside_end_jump_offset = 0x2097D;
    inline constexpr size_t runtime_final_world_guidance_ew04_choice_call_offset = 0x8BD8;
    inline constexpr size_t runtime_final_world_guidance_ew04_choice_pop_offset = 0x8C5E;
    inline constexpr size_t runtime_final_world_guidance_ew04_third_choice_jump_offset = 0x8CE1;
    inline constexpr size_t runtime_final_world_guidance_ew04_third_choice_branch_offset = 0x8CF0;
    inline constexpr size_t runtime_final_world_guidance_ew04_message_pop_offset = 0x8E37;
    inline constexpr size_t runtime_final_world_guidance_ew04_resume_offset = 0x8E38;

    inline constexpr std::wstring_view runtime_final_world_guidance_level_path_prefix =
            L"/Game/Levels/ew/ew_01/";
    inline constexpr std::wstring_view runtime_final_world_guidance_darkside_transition_path_prefix =
            L"/Game/Levels/ew_DLC/ew_04/";
    inline constexpr std::wstring_view runtime_final_world_guidance_ew01_gameflow_class_name =
            L"ew_01_gameflow_C";
    inline constexpr std::wstring_view runtime_final_world_guidance_ew04_gameflow_class_name =
            L"ew_04_gameflow_C";
    inline constexpr std::wstring_view runtime_final_world_guidance_ew01_function_path =
            L"/Game/Levels/ew/ew_01/umap/ew_01_gameflow.ew_01_gameflow_C:ExecuteUbergraph_ew_01_gameflow";
    inline constexpr std::wstring_view runtime_final_world_guidance_ew04_function_path =
            L"/Game/Levels/ew_DLC/ew_04/umap/ew_04_gameflow.ew_04_gameflow_C:ExecuteUbergraph_ew_04_gameflow";

    auto is_runtime_final_world_guidance_bypass_context(
            std::wstring_view context_hint) noexcept -> bool;

    auto try_patch_runtime_final_world_guidance_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeFinalWorldGuidanceBypassResult;
}
