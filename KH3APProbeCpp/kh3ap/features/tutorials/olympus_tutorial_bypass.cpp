#include "kh3ap/features/tutorials/olympus_tutorial_bypass.h"

#include <algorithm>
#include <array>

namespace kh3ap
{
    namespace
    {
        constexpr std::array<uint8_t, 5> s_original_condition{
                0x07,
                0x44, 0x07, 0x00, 0x00,
        };
        // The previous implementation retargeted this branch and skipped the
        // randomized EVENT_KEYBLADE_001 reward along with the tutorials. Accept
        // that exact legacy state so a loaded function can be migrated safely.
        constexpr std::array<uint8_t, 5> s_legacy_patched_condition{
                0x07,
                0x2B, 0x06, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 5> s_reward_latent_linkage{
                0x5B,
                0xCA, 0x07, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 5> s_suppressed_reward_call{0x06, 0xCA, 0x07, 0x00, 0x00};
        constexpr std::array<uint8_t, 6> s_original_post_reward_tutorial_gate{
                0x07,
                0x2F, 0x0A, 0x00, 0x00,
                0x27,
        };
        constexpr std::array<uint8_t, 6> s_patched_post_reward_tutorial_gate{
                0x07,
                0x2B, 0x06, 0x00, 0x00,
                0x28,
        };
        constexpr std::array<uint8_t, 5> s_formchange_help_latent_linkage{
                0x5B,
                0x68, 0x08, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 3> s_formchange_help_id{
                0x24, 0x07, 0x28,
        };
        constexpr std::array<uint8_t, 5> s_counter_shield_help_latent_linkage{
                0x5B,
                0x0C, 0x09, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 3> s_counter_shield_help_id{
                0x24, 0x24, 0x28,
        };
        constexpr std::array<uint8_t, 5> s_camp_menu_latent_linkage{
                0x5B,
                0xA7, 0x09, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 5> s_tutorial_return_jump{
                0x06,
                0x2B, 0x06, 0x00, 0x00,
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
    }

    auto try_patch_runtime_olympus_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeOlympusTutorialBypassResult
    {
        if (!script || script_size != runtime_olympus_tutorial_script_size)
        {
            return RuntimeOlympusTutorialBypassResult::Rejected;
        }

        const bool has_original_condition = matches_at(
                script,
                script_size,
                runtime_olympus_tutorial_condition_offset,
                s_original_condition);
        const bool has_legacy_patched_condition = matches_at(
                script,
                script_size,
                runtime_olympus_tutorial_condition_offset,
                s_legacy_patched_condition);
        const bool has_original_post_reward_tutorial_gate = matches_at(
                script,
                script_size,
                runtime_olympus_post_reward_tutorial_gate_offset,
                s_original_post_reward_tutorial_gate);
        const bool has_patched_post_reward_tutorial_gate = matches_at(
                script,
                script_size,
                runtime_olympus_post_reward_tutorial_gate_offset,
                s_patched_post_reward_tutorial_gate);
        if (has_original_condition == has_legacy_patched_condition ||
            has_original_post_reward_tutorial_gate == has_patched_post_reward_tutorial_gate ||
            script[runtime_olympus_tutorial_condition_offset + s_original_condition.size()] != 0x00 ||
            script[runtime_olympus_tutorial_fallthrough_offset] != 0x19 ||
            script[runtime_olympus_reward_block_offset] != 0x0F ||
            (!(script[runtime_olympus_reward_call_offset] == 0x19 &&
               script[runtime_olympus_reward_call_offset + 1] == 0x20) &&
             !matches_at(script, script_size, runtime_olympus_reward_call_offset, s_suppressed_reward_call)) ||
            script[runtime_olympus_reward_final_function_offset] != 0x1C ||
            !matches_at(
                    script,
                    script_size,
                    runtime_olympus_reward_latent_linkage_offset,
                    s_reward_latent_linkage) ||
            script[runtime_olympus_post_reward_delay_offset] != 0x19 ||
            script[runtime_olympus_post_reward_delay_offset + 1] != 0x20 ||
            script[runtime_olympus_post_reward_tutorial_gate_offset - 1] != 0x4D ||
            script[runtime_olympus_formchange_help_call_offset] != 0x19 ||
            script[runtime_olympus_formchange_help_call_offset + 1] != 0x20 ||
            script[runtime_olympus_formchange_help_final_function_offset] != 0x1C ||
            !matches_at(
                    script,
                    script_size,
                    runtime_olympus_formchange_help_latent_linkage_offset,
                    s_formchange_help_latent_linkage) ||
            !matches_at(
                    script,
                    script_size,
                    runtime_olympus_formchange_help_id_offset,
                    s_formchange_help_id) ||
            script[runtime_olympus_counter_shield_help_call_offset] != 0x19 ||
            script[runtime_olympus_counter_shield_help_call_offset + 1] != 0x20 ||
            script[runtime_olympus_counter_shield_help_final_function_offset] != 0x1C ||
            !matches_at(
                    script,
                    script_size,
                    runtime_olympus_counter_shield_help_latent_linkage_offset,
                    s_counter_shield_help_latent_linkage) ||
            !matches_at(
                    script,
                    script_size,
                    runtime_olympus_counter_shield_help_id_offset,
                    s_counter_shield_help_id) ||
            script[runtime_olympus_camp_menu_call_offset] != 0x19 ||
            script[runtime_olympus_camp_menu_call_offset + 1] != 0x20 ||
            script[runtime_olympus_camp_menu_final_function_offset] != 0x1C ||
            !matches_at(
                    script,
                    script_size,
                    runtime_olympus_camp_menu_latent_linkage_offset,
                    s_camp_menu_latent_linkage) ||
            !matches_at(
                    script,
                    script_size,
                    runtime_olympus_tutorial_return_jump_offset,
                    s_tutorial_return_jump))
        {
            return RuntimeOlympusTutorialBypassResult::Rejected;
        }

        if (has_original_condition && has_patched_post_reward_tutorial_gate)
        {
            return RuntimeOlympusTutorialBypassResult::AlreadyPatched;
        }

        if (has_legacy_patched_condition)
        {
            std::copy(
                    s_original_condition.begin() + 1,
                    s_original_condition.end(),
                    script + runtime_olympus_tutorial_condition_offset + 1);
        }
        if (has_original_post_reward_tutorial_gate)
        {
            std::copy(
                    s_patched_post_reward_tutorial_gate.begin() + 1,
                    s_patched_post_reward_tutorial_gate.end(),
                    script + runtime_olympus_post_reward_tutorial_gate_offset + 1);
        }

        return matches_at(
                       script,
                       script_size,
                       runtime_olympus_tutorial_condition_offset,
                       s_original_condition) &&
                       matches_at(
                               script,
                               script_size,
                               runtime_olympus_post_reward_tutorial_gate_offset,
                               s_patched_post_reward_tutorial_gate)
                ? RuntimeOlympusTutorialBypassResult::Patched
                : RuntimeOlympusTutorialBypassResult::Rejected;
    }
}
