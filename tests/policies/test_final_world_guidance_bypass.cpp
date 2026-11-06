#include "kh3ap/features/story/world_fixes/final_world_guidance_bypass.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace
{
    struct TestProfile
    {
        size_t script_size{};
        size_t gate_offset{};
        size_t prompt_offset{};
        size_t gate_false_offset{};
        size_t darkside_end_jump_offset{};
        size_t choice_call_offset{};
        size_t choice_pop_offset{};
        size_t third_choice_jump_offset{};
        size_t third_choice_branch_offset{};
        size_t message_pop_offset{};
        size_t resume_offset{};
        std::array<uint8_t, 5> original_gate{};
        std::array<uint8_t, 5> previous_gate_false_bypass{};
        std::array<uint8_t, 5> patched_gate{};
        std::array<uint8_t, 5> original_darkside_end_jump{};
        std::array<uint8_t, 5> previous_gate_false_darkside_end_jump{};
        std::array<uint8_t, 5> patched_darkside_end_jump{};
        std::array<uint8_t, 5> third_choice_jump{};
    };

    constexpr TestProfile ew01_profile{
            kh3ap::runtime_final_world_guidance_ew01_script_size,
            kh3ap::runtime_final_world_guidance_ew01_gate_offset,
            kh3ap::runtime_final_world_guidance_ew01_prompt_offset,
            kh3ap::runtime_final_world_guidance_ew01_gate_false_offset,
            kh3ap::runtime_final_world_guidance_ew01_darkside_end_jump_offset,
            kh3ap::runtime_final_world_guidance_ew01_choice_call_offset,
            kh3ap::runtime_final_world_guidance_ew01_choice_pop_offset,
            kh3ap::runtime_final_world_guidance_ew01_third_choice_jump_offset,
            kh3ap::runtime_final_world_guidance_ew01_third_choice_branch_offset,
            kh3ap::runtime_final_world_guidance_ew01_message_pop_offset,
            kh3ap::runtime_final_world_guidance_ew01_resume_offset,
            {0x07, 0xE3, 0xA8, 0x00, 0x00},
            {0x06, 0xE3, 0xA8, 0x00, 0x00},
            {0x06, 0x16, 0x8B, 0x00, 0x00},
            {0x06, 0x7A, 0x85, 0x00, 0x00},
            {0x06, 0xE3, 0xA8, 0x00, 0x00},
            {0x06, 0x16, 0x8B, 0x00, 0x00},
            {0x07, 0xCE, 0x89, 0x00, 0x00},
    };

    constexpr TestProfile ew04_profile{
            kh3ap::runtime_final_world_guidance_ew04_script_size,
            kh3ap::runtime_final_world_guidance_ew04_gate_offset,
            kh3ap::runtime_final_world_guidance_ew04_prompt_offset,
            kh3ap::runtime_final_world_guidance_ew04_gate_false_offset,
            kh3ap::runtime_final_world_guidance_ew04_darkside_end_jump_offset,
            kh3ap::runtime_final_world_guidance_ew04_choice_call_offset,
            kh3ap::runtime_final_world_guidance_ew04_choice_pop_offset,
            kh3ap::runtime_final_world_guidance_ew04_third_choice_jump_offset,
            kh3ap::runtime_final_world_guidance_ew04_third_choice_branch_offset,
            kh3ap::runtime_final_world_guidance_ew04_message_pop_offset,
            kh3ap::runtime_final_world_guidance_ew04_resume_offset,
            {0x07, 0x27, 0xAA, 0x00, 0x00},
            {0x06, 0x27, 0xAA, 0x00, 0x00},
            {0x06, 0x38, 0x8E, 0x00, 0x00},
            {0x06, 0x9C, 0x88, 0x00, 0x00},
            {0x06, 0x27, 0xAA, 0x00, 0x00},
            {0x06, 0x38, 0x8E, 0x00, 0x00},
            {0x07, 0xF0, 0x8C, 0x00, 0x00},
    };

    template <size_t Size>
    auto write_pattern(
            std::vector<uint8_t>& script,
            size_t offset,
            const std::array<uint8_t, Size>& pattern) -> void
    {
        std::copy(pattern.begin(), pattern.end(), script.begin() + offset);
    }

    auto make_valid_script(
            const TestProfile& profile,
            const std::array<uint8_t, 5>& gate,
            const std::array<uint8_t, 5>& darkside_end_jump) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> script(profile.script_size, 0xCC);
        write_pattern(script, profile.gate_offset, gate);
        script[profile.gate_offset + gate.size()] = 0x01;
        script[profile.prompt_offset] = 0x19;
        script[profile.gate_false_offset] = 0x19;
        write_pattern(
                script,
                profile.darkside_end_jump_offset,
                darkside_end_jump);
        script[profile.darkside_end_jump_offset + darkside_end_jump.size()] = 0x06;
        script[profile.choice_call_offset] = 0x0F;
        script[profile.choice_pop_offset] = 0x4D;
        write_pattern(
                script,
                profile.third_choice_jump_offset,
                profile.third_choice_jump);
        script[profile.third_choice_branch_offset - 1] = 0x4D;
        script[profile.third_choice_branch_offset] = 0x0F;
        script[profile.message_pop_offset] = 0x4D;
        script[profile.resume_offset] = 0x14;
        return script;
    }

    auto run_profile_tests(const TestProfile& profile) -> void
    {
        {
            auto script = make_valid_script(
                    profile,
                    profile.original_gate,
                    profile.original_darkside_end_jump);
            const auto before = script;
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Patched);
            for (size_t index = 0; index < script.size(); ++index)
            {
                const bool is_gate_byte =
                        index >= profile.gate_offset &&
                        index < profile.gate_offset + profile.patched_gate.size();
                const bool is_darkside_end_jump_byte =
                        index >= profile.darkside_end_jump_offset &&
                        index < profile.darkside_end_jump_offset +
                                        profile.patched_darkside_end_jump.size();
                assert(is_gate_byte ||
                       is_darkside_end_jump_byte ||
                       script[index] == before[index]);
            }
            assert(std::equal(
                    profile.patched_gate.begin(),
                    profile.patched_gate.end(),
                    script.begin() + profile.gate_offset));
            assert(std::equal(
                    profile.patched_darkside_end_jump.begin(),
                    profile.patched_darkside_end_jump.end(),
                    script.begin() + profile.darkside_end_jump_offset));

            const auto after_patch = script;
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::AlreadyPatched);
            assert(script == after_patch);
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.previous_gate_false_bypass,
                    profile.original_darkside_end_jump);
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Patched);
            assert(std::equal(
                    profile.patched_gate.begin(),
                    profile.patched_gate.end(),
                    script.begin() + profile.gate_offset));
            assert(std::equal(
                    profile.patched_darkside_end_jump.begin(),
                    profile.patched_darkside_end_jump.end(),
                    script.begin() + profile.darkside_end_jump_offset));
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.previous_gate_false_bypass,
                    profile.previous_gate_false_darkside_end_jump);
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Patched);
            assert(std::equal(
                    profile.patched_gate.begin(),
                    profile.patched_gate.end(),
                    script.begin() + profile.gate_offset));
            assert(std::equal(
                    profile.patched_darkside_end_jump.begin(),
                    profile.patched_darkside_end_jump.end(),
                    script.begin() + profile.darkside_end_jump_offset));
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.patched_gate,
                    profile.original_darkside_end_jump);
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Patched);
            assert(std::equal(
                    profile.patched_darkside_end_jump.begin(),
                    profile.patched_darkside_end_jump.end(),
                    script.begin() + profile.darkside_end_jump_offset));
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.original_gate,
                    profile.patched_darkside_end_jump);
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Patched);
            assert(std::equal(
                    profile.patched_gate.begin(),
                    profile.patched_gate.end(),
                    script.begin() + profile.gate_offset));
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.patched_gate,
                    profile.patched_darkside_end_jump);
            const auto before = script;
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::AlreadyPatched);
            assert(script == before);
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.original_gate,
                    profile.original_darkside_end_jump);
            script[profile.gate_offset + profile.original_gate.size()] = 0x00;
            const auto before = script;
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.original_gate,
                    profile.original_darkside_end_jump);
            script[profile.darkside_end_jump_offset + 1] ^= 0x01;
            const auto before = script;
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.original_gate,
                    profile.original_darkside_end_jump);
            script[profile.third_choice_jump_offset] = 0x06;
            const auto before = script;
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(
                    profile,
                    profile.original_gate,
                    profile.original_darkside_end_jump);
            script[profile.resume_offset] = 0x18;
            const auto before = script;
            assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                           script.data(),
                           script.size()) ==
                   kh3ap::RuntimeFinalWorldGuidanceBypassResult::Rejected);
            assert(script == before);
        }
    }
}

int main()
{
    assert(kh3ap::is_runtime_final_world_guidance_bypass_context(
            L"Function /Game/Levels/ew/ew_01/umap/ew_01_camera_ms070."
            L"ew_01_camera_ms070_C:ExecuteUbergraph_ew_01_camera_ms070"));
    assert(kh3ap::is_runtime_final_world_guidance_bypass_context(
            L"Function /Game/Levels/ew_DLC/ew_04/umap/ew_04_camera_ms210."
            L"ew_04_camera_ms210_C:ExecuteUbergraph_ew_04_camera_ms210"));
    assert(!kh3ap::is_runtime_final_world_guidance_bypass_context(
            L"Function /Game/Levels/ew/ew_02/umap/ew_02_camera."
            L"ew_02_camera_C:ExecuteUbergraph_ew_02_camera"));

    run_profile_tests(ew01_profile);
    run_profile_tests(ew04_profile);

    {
        std::vector<uint8_t> script(1024, 0xCC);
        const auto before = script;
        assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeFinalWorldGuidanceBypassResult::Rejected);
        assert(script == before);
    }

    assert(kh3ap::try_patch_runtime_final_world_guidance_bytecode(
                   nullptr,
                   kh3ap::runtime_final_world_guidance_ew04_script_size) ==
           kh3ap::RuntimeFinalWorldGuidanceBypassResult::Rejected);

    return 0;
}
