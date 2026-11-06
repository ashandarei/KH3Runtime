#include "kh3ap/features/tutorials/olympus_tutorial_bypass.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace
{
    constexpr std::array<uint8_t, 5> original_condition{
            0x07,
            0x44, 0x07, 0x00, 0x00,
    };
    constexpr std::array<uint8_t, 5> legacy_patched_condition{
            0x07,
            0x2B, 0x06, 0x00, 0x00,
    };
    constexpr std::array<uint8_t, 5> reward_latent_linkage{
            0x5B,
            0xCA, 0x07, 0x00, 0x00,
    };
    constexpr std::array<uint8_t, 6> original_post_reward_tutorial_gate{
            0x07,
            0x2F, 0x0A, 0x00, 0x00,
            0x27,
    };
    constexpr std::array<uint8_t, 6> patched_post_reward_tutorial_gate{
            0x07,
            0x2B, 0x06, 0x00, 0x00,
            0x28,
    };
    constexpr std::array<uint8_t, 5> formchange_help_latent_linkage{
            0x5B,
            0x68, 0x08, 0x00, 0x00,
    };
    constexpr std::array<uint8_t, 3> formchange_help_id{
            0x24, 0x07, 0x28,
    };
    constexpr std::array<uint8_t, 5> counter_shield_help_latent_linkage{
            0x5B,
            0x0C, 0x09, 0x00, 0x00,
    };
    constexpr std::array<uint8_t, 3> counter_shield_help_id{
            0x24, 0x24, 0x28,
    };
    constexpr std::array<uint8_t, 5> camp_menu_latent_linkage{
            0x5B,
            0xA7, 0x09, 0x00, 0x00,
    };
    constexpr std::array<uint8_t, 5> return_jump{
            0x06,
            0x2B, 0x06, 0x00, 0x00,
    };

    template <size_t Size>
    auto write_pattern(
            std::vector<uint8_t>& script,
            size_t offset,
            const std::array<uint8_t, Size>& pattern) -> void
    {
        for (size_t index = 0; index < Size; ++index)
        {
            script[offset + index] = pattern[index];
        }
    }

    auto make_valid_script() -> std::vector<uint8_t>
    {
        std::vector<uint8_t> script(
                kh3ap::runtime_olympus_tutorial_script_size,
                0xCC);
        write_pattern(
                script,
                kh3ap::runtime_olympus_tutorial_condition_offset,
                original_condition);
        script[kh3ap::runtime_olympus_tutorial_condition_offset + original_condition.size()] = 0x00;
        script[kh3ap::runtime_olympus_tutorial_fallthrough_offset] = 0x19;
        script[kh3ap::runtime_olympus_reward_block_offset] = 0x0F;
        script[kh3ap::runtime_olympus_reward_call_offset] = 0x19;
        script[kh3ap::runtime_olympus_reward_call_offset + 1] = 0x20;
        script[kh3ap::runtime_olympus_reward_final_function_offset] = 0x1C;
        write_pattern(
                script,
                kh3ap::runtime_olympus_reward_latent_linkage_offset,
                reward_latent_linkage);
        script[kh3ap::runtime_olympus_post_reward_delay_offset] = 0x19;
        script[kh3ap::runtime_olympus_post_reward_delay_offset + 1] = 0x20;
        script[kh3ap::runtime_olympus_post_reward_tutorial_gate_offset - 1] = 0x4D;
        write_pattern(
                script,
                kh3ap::runtime_olympus_post_reward_tutorial_gate_offset,
                original_post_reward_tutorial_gate);
        script[kh3ap::runtime_olympus_formchange_help_call_offset] = 0x19;
        script[kh3ap::runtime_olympus_formchange_help_call_offset + 1] = 0x20;
        script[kh3ap::runtime_olympus_formchange_help_final_function_offset] = 0x1C;
        write_pattern(
                script,
                kh3ap::runtime_olympus_formchange_help_latent_linkage_offset,
                formchange_help_latent_linkage);
        write_pattern(
                script,
                kh3ap::runtime_olympus_formchange_help_id_offset,
                formchange_help_id);
        script[kh3ap::runtime_olympus_counter_shield_help_call_offset] = 0x19;
        script[kh3ap::runtime_olympus_counter_shield_help_call_offset + 1] = 0x20;
        script[kh3ap::runtime_olympus_counter_shield_help_final_function_offset] = 0x1C;
        write_pattern(
                script,
                kh3ap::runtime_olympus_counter_shield_help_latent_linkage_offset,
                counter_shield_help_latent_linkage);
        write_pattern(
                script,
                kh3ap::runtime_olympus_counter_shield_help_id_offset,
                counter_shield_help_id);
        script[kh3ap::runtime_olympus_camp_menu_call_offset] = 0x19;
        script[kh3ap::runtime_olympus_camp_menu_call_offset + 1] = 0x20;
        script[kh3ap::runtime_olympus_camp_menu_final_function_offset] = 0x1C;
        write_pattern(
                script,
                kh3ap::runtime_olympus_camp_menu_latent_linkage_offset,
                camp_menu_latent_linkage);
        write_pattern(
                script,
                kh3ap::runtime_olympus_tutorial_return_jump_offset,
                return_jump);
        return script;
    }
}

int main()
{
    {
        // The independent story-reward guard can run first on object creation.
        // Its jump must not prevent the existing tutorial skip from applying.
        auto script = make_valid_script();
        write_pattern(script, kh3ap::runtime_olympus_reward_call_offset,
            std::array<uint8_t, 5>{0x06, 0xCA, 0x07, 0x00, 0x00});
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(script.data(), script.size()) ==
            kh3ap::RuntimeOlympusTutorialBypassResult::Patched);
        assert(script[kh3ap::runtime_olympus_reward_call_offset] == 0x06);
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(script.data(), script.size()) ==
            kh3ap::RuntimeOlympusTutorialBypassResult::AlreadyPatched);
    }
    {
        auto script = make_valid_script();
        const auto before = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::Patched);
        for (size_t index = 0; index < script.size(); ++index)
        {
            const bool is_retargeted_byte =
                    index >= kh3ap::runtime_olympus_post_reward_tutorial_gate_offset + 1 &&
                    index <= kh3ap::runtime_olympus_post_reward_tutorial_gate_offset + 5;
            assert(is_retargeted_byte || script[index] == before[index]);
        }
        for (size_t index = 0; index < original_condition.size(); ++index)
        {
            assert(script[kh3ap::runtime_olympus_tutorial_condition_offset + index] ==
                   original_condition[index]);
        }
        for (size_t index = 0; index < patched_post_reward_tutorial_gate.size(); ++index)
        {
            assert(script[kh3ap::runtime_olympus_post_reward_tutorial_gate_offset + index] ==
                   patched_post_reward_tutorial_gate[index]);
        }

        const auto after_patch = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::AlreadyPatched);
        assert(script == after_patch);
    }

    {
        auto script = make_valid_script();
        write_pattern(
                script,
                kh3ap::runtime_olympus_tutorial_condition_offset,
                legacy_patched_condition);
        const auto before = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::Patched);
        for (size_t index = 0; index < script.size(); ++index)
        {
            const bool is_restored_legacy_condition_byte =
                    index >= kh3ap::runtime_olympus_tutorial_condition_offset + 1 &&
                    index <= kh3ap::runtime_olympus_tutorial_condition_offset + 4;
            const bool is_retargeted_tutorial_gate_byte =
                    index >= kh3ap::runtime_olympus_post_reward_tutorial_gate_offset + 1 &&
                    index <= kh3ap::runtime_olympus_post_reward_tutorial_gate_offset + 5;
            assert(is_restored_legacy_condition_byte ||
                   is_retargeted_tutorial_gate_byte ||
                   script[index] == before[index]);
        }
        for (size_t index = 0; index < original_condition.size(); ++index)
        {
            assert(script[kh3ap::runtime_olympus_tutorial_condition_offset + index] ==
                   original_condition[index]);
        }
        for (size_t index = 0; index < patched_post_reward_tutorial_gate.size(); ++index)
        {
            assert(script[kh3ap::runtime_olympus_post_reward_tutorial_gate_offset + index] ==
                   patched_post_reward_tutorial_gate[index]);
        }
    }

    {
        auto script = make_valid_script();
        script.pop_back();
        const auto before = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::Rejected);
        assert(script == before);
    }

    {
        auto script = make_valid_script();
        script[kh3ap::runtime_olympus_tutorial_condition_offset] = 0x06;
        const auto before = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::Rejected);
        assert(script == before);
    }

    {
        auto script = make_valid_script();
        script[kh3ap::runtime_olympus_reward_latent_linkage_offset + 1] = 0xCB;
        const auto before = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::Rejected);
        assert(script == before);
    }

    {
        auto script = make_valid_script();
        script[kh3ap::runtime_olympus_formchange_help_id_offset + 1] = 0x08;
        const auto before = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::Rejected);
        assert(script == before);
    }

    {
        auto script = make_valid_script();
        script[kh3ap::runtime_olympus_tutorial_return_jump_offset] = 0x07;
        const auto before = script;
        assert(kh3ap::try_patch_runtime_olympus_tutorial_bytecode(
                       script.data(),
                       script.size()) ==
               kh3ap::RuntimeOlympusTutorialBypassResult::Rejected);
        assert(script == before);
    }

    return 0;
}
