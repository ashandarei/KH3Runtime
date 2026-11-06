#include "kh3ap/features/story/world_fixes/final_world_guidance_bypass.h"

#include <algorithm>
#include <array>

namespace kh3ap
{
    namespace
    {
        struct GuidanceBytecodeProfile
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

        constexpr GuidanceBytecodeProfile s_ew01_profile{
                runtime_final_world_guidance_ew01_script_size,
                runtime_final_world_guidance_ew01_gate_offset,
                runtime_final_world_guidance_ew01_prompt_offset,
                runtime_final_world_guidance_ew01_gate_false_offset,
                runtime_final_world_guidance_ew01_darkside_end_jump_offset,
                runtime_final_world_guidance_ew01_choice_call_offset,
                runtime_final_world_guidance_ew01_choice_pop_offset,
                runtime_final_world_guidance_ew01_third_choice_jump_offset,
                runtime_final_world_guidance_ew01_third_choice_branch_offset,
                runtime_final_world_guidance_ew01_message_pop_offset,
                runtime_final_world_guidance_ew01_resume_offset,
                {0x07, 0xE3, 0xA8, 0x00, 0x00},
                {0x06, 0xE3, 0xA8, 0x00, 0x00},
                {0x06, 0x16, 0x8B, 0x00, 0x00},
                {0x06, 0x7A, 0x85, 0x00, 0x00},
                {0x06, 0xE3, 0xA8, 0x00, 0x00},
                {0x06, 0x16, 0x8B, 0x00, 0x00},
                {0x07, 0xCE, 0x89, 0x00, 0x00},
        };

        constexpr GuidanceBytecodeProfile s_ew04_profile{
                runtime_final_world_guidance_ew04_script_size,
                runtime_final_world_guidance_ew04_gate_offset,
                runtime_final_world_guidance_ew04_prompt_offset,
                runtime_final_world_guidance_ew04_gate_false_offset,
                runtime_final_world_guidance_ew04_darkside_end_jump_offset,
                runtime_final_world_guidance_ew04_choice_call_offset,
                runtime_final_world_guidance_ew04_choice_pop_offset,
                runtime_final_world_guidance_ew04_third_choice_jump_offset,
                runtime_final_world_guidance_ew04_third_choice_branch_offset,
                runtime_final_world_guidance_ew04_message_pop_offset,
                runtime_final_world_guidance_ew04_resume_offset,
                {0x07, 0x27, 0xAA, 0x00, 0x00},
                {0x06, 0x27, 0xAA, 0x00, 0x00},
                {0x06, 0x38, 0x8E, 0x00, 0x00},
                {0x06, 0x9C, 0x88, 0x00, 0x00},
                {0x06, 0x27, 0xAA, 0x00, 0x00},
                {0x06, 0x38, 0x8E, 0x00, 0x00},
                {0x07, 0xF0, 0x8C, 0x00, 0x00},
        };

        template <size_t PatternSize>
        auto matches_at(
                const uint8_t* script,
                size_t script_size,
                size_t offset,
                const std::array<uint8_t, PatternSize>& pattern) noexcept -> bool
        {
            return script &&
                   offset <= script_size &&
                   PatternSize <= script_size - offset &&
                   std::equal(pattern.begin(), pattern.end(), script + offset);
        }

        auto profile_for_script_size(size_t script_size) noexcept
                -> const GuidanceBytecodeProfile*
        {
            if (script_size == s_ew01_profile.script_size)
            {
                return &s_ew01_profile;
            }
            if (script_size == s_ew04_profile.script_size)
            {
                return &s_ew04_profile;
            }
            return nullptr;
        }
    }

    auto is_runtime_final_world_guidance_bypass_context(
            std::wstring_view context_hint) noexcept -> bool
    {
        return context_hint.find(runtime_final_world_guidance_level_path_prefix) !=
                       std::wstring_view::npos ||
               context_hint.find(runtime_final_world_guidance_darkside_transition_path_prefix) !=
                       std::wstring_view::npos;
    }

    auto try_patch_runtime_final_world_guidance_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeFinalWorldGuidanceBypassResult
    {
        const GuidanceBytecodeProfile* const profile =
                profile_for_script_size(script_size);
        if (!script || !profile)
        {
            return RuntimeFinalWorldGuidanceBypassResult::Rejected;
        }

        const bool has_original_gate = matches_at(
                script,
                script_size,
                profile->gate_offset,
                profile->original_gate);
        const bool has_previous_gate_false_bypass = matches_at(
                script,
                script_size,
                profile->gate_offset,
                profile->previous_gate_false_bypass);
        const bool has_patched_gate = matches_at(
                script,
                script_size,
                profile->gate_offset,
                profile->patched_gate);
        const int matching_gate_count =
                static_cast<int>(has_original_gate) +
                static_cast<int>(has_previous_gate_false_bypass) +
                static_cast<int>(has_patched_gate);
        const bool has_original_darkside_end_jump = matches_at(
                script,
                script_size,
                profile->darkside_end_jump_offset,
                profile->original_darkside_end_jump);
        const bool has_previous_gate_false_darkside_end_jump = matches_at(
                script,
                script_size,
                profile->darkside_end_jump_offset,
                profile->previous_gate_false_darkside_end_jump);
        const bool has_patched_darkside_end_jump = matches_at(
                script,
                script_size,
                profile->darkside_end_jump_offset,
                profile->patched_darkside_end_jump);
        const int matching_darkside_end_jump_count =
                static_cast<int>(has_original_darkside_end_jump) +
                static_cast<int>(has_previous_gate_false_darkside_end_jump) +
                static_cast<int>(has_patched_darkside_end_jump);
        if (matching_gate_count != 1 ||
            matching_darkside_end_jump_count != 1 ||
            script[profile->gate_offset + profile->original_gate.size()] != 0x01 ||
            script[profile->prompt_offset] != 0x19 ||
            script[profile->gate_false_offset] != 0x19 ||
            script[profile->darkside_end_jump_offset +
                   profile->original_darkside_end_jump.size()] != 0x06 ||
            script[profile->choice_call_offset] != 0x0F ||
            script[profile->choice_pop_offset] != 0x4D ||
            !matches_at(
                    script,
                    script_size,
                    profile->third_choice_jump_offset,
                    profile->third_choice_jump) ||
            script[profile->third_choice_branch_offset - 1] != 0x4D ||
            script[profile->third_choice_branch_offset] != 0x0F ||
            script[profile->message_pop_offset] != 0x4D ||
            script[profile->resume_offset] != 0x14)
        {
            return RuntimeFinalWorldGuidanceBypassResult::Rejected;
        }

        if (has_patched_gate && has_patched_darkside_end_jump)
        {
            return RuntimeFinalWorldGuidanceBypassResult::AlreadyPatched;
        }

        // The Darkside-ending event bypasses the BattleTutoAgain gate with a
        // second, unconditional jump directly to the prompt. Route both entry
        // paths to the latent continuation reached after the third choice's
        // follow-up message. The gate's false branch re-enters the Darkside
        // event setup and must not be used as the skip destination.
        std::copy(
                profile->patched_gate.begin(),
                profile->patched_gate.end(),
                script + profile->gate_offset);
        std::copy(
                profile->patched_darkside_end_jump.begin(),
                profile->patched_darkside_end_jump.end(),
                script + profile->darkside_end_jump_offset);
        return matches_at(
                       script,
                       script_size,
                       profile->gate_offset,
                       profile->patched_gate) &&
                       matches_at(
                               script,
                               script_size,
                               profile->darkside_end_jump_offset,
                               profile->patched_darkside_end_jump)
                ? RuntimeFinalWorldGuidanceBypassResult::Patched
                : RuntimeFinalWorldGuidanceBypassResult::Rejected;
    }
}
