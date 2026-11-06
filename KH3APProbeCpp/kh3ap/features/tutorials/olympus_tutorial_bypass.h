#pragma once

#include <cstddef>
#include <cstdint>

namespace kh3ap
{
    enum class RuntimeOlympusTutorialBypassResult
    {
        Patched,
        AlreadyPatched,
        Rejected,
    };

    inline constexpr size_t runtime_olympus_tutorial_script_size = 0x2901B;
    inline constexpr size_t runtime_olympus_tutorial_condition_offset = 0x61D;
    inline constexpr size_t runtime_olympus_tutorial_fallthrough_offset = 0x62B;
    inline constexpr size_t runtime_olympus_reward_block_offset = 0x744;
    inline constexpr size_t runtime_olympus_reward_call_offset = 0x779;
    inline constexpr size_t runtime_olympus_reward_final_function_offset = 0x78F;
    inline constexpr size_t runtime_olympus_reward_latent_linkage_offset = 0x7A6;
    inline constexpr size_t runtime_olympus_post_reward_delay_offset = 0x7CA;
    inline constexpr size_t runtime_olympus_post_reward_tutorial_gate_offset = 0x817;
    inline constexpr size_t runtime_olympus_formchange_help_call_offset = 0x81D;
    inline constexpr size_t runtime_olympus_formchange_help_final_function_offset = 0x833;
    inline constexpr size_t runtime_olympus_formchange_help_latent_linkage_offset = 0x84A;
    inline constexpr size_t runtime_olympus_formchange_help_id_offset = 0x863;
    inline constexpr size_t runtime_olympus_counter_shield_help_call_offset = 0x8C1;
    inline constexpr size_t runtime_olympus_counter_shield_help_final_function_offset = 0x8D7;
    inline constexpr size_t runtime_olympus_counter_shield_help_latent_linkage_offset = 0x8EE;
    inline constexpr size_t runtime_olympus_counter_shield_help_id_offset = 0x907;
    inline constexpr size_t runtime_olympus_camp_menu_call_offset = 0x95F;
    inline constexpr size_t runtime_olympus_camp_menu_final_function_offset = 0x975;
    inline constexpr size_t runtime_olympus_camp_menu_latent_linkage_offset = 0x98C;
    inline constexpr size_t runtime_olympus_tutorial_return_jump_offset = 0x9A7;

    auto try_patch_runtime_olympus_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeOlympusTutorialBypassResult;
}
