#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeTutorialPopupBypassResult
    {
        Patched,
        AlreadyPatched,
        Rejected,
    };

    struct RuntimeTutorialFollowupHelpAnchor
    {
        size_t callsite_offset{};
        size_t final_function_offset{};
        size_t latent_linkage_offset{};
        size_t latent_resume_offset{};
        size_t latent_resume_jump_target{};
        size_t message_id_offset{};
        uint8_t message_id{};
    };

    // Describes one tutorial sequence whose false/declined path can be entered
    // directly. The bypass entry may precede the prompt when tutorial-owned
    // state changes must also be skipped. Profiles anchor the entry, exact
    // prompt, message id/key (when the call embeds a key), latent linkage,
    // tutorial path, and bypass path before any byte is changed. Additional
    // tutorial windows can reuse the same patcher rather than adding a
    // process-wide UI hook.
    struct RuntimeTutorialPopupBypassProfile
    {
        size_t script_size{};
        size_t bypass_entry_offset{};
        uint8_t bypass_entry_object_opcode{};
        size_t tutorial_callsite_offset{};
        size_t final_function_offset{};
        size_t latent_linkage_offset{};
        size_t latent_resume_offset{};
        size_t tutorial_continuation_offset{};
        uint8_t tutorial_continuation_opcode{};
        size_t bypass_continuation_offset{};
        uint8_t bypass_continuation_opcode{};
        size_t message_id_offset{};
        uint8_t message_id{};
        size_t message_key_offset{};
        std::string_view message_key{};
        // Some one-way help calls resume directly into a post-dismiss block
        // instead of testing an OutExecMessage branch. The bypass may enter
        // that block or skip tutorial-owned follow-up UI inside it.
        bool latent_resume_has_no_bypass_branch{false};
        // A direct resume can itself be an EX_JumpIfNot. When nonzero, anchor
        // its exact target plus its EX_True condition rather than accepting
        // only the opcode at the resume offset.
        size_t latent_resume_direct_jump_target{};
        // A declined path can contain another help window before it reaches
        // the state-preserving continuation. When nonzero, patch directly to
        // this later continuation while retaining the original declined
        // branch as a bytecode anchor.
        size_t patched_bypass_continuation_offset{};
        uint8_t patched_bypass_continuation_opcode{};
        RuntimeTutorialFollowupHelpAnchor bypass_followup_help{};
    };

    auto try_patch_runtime_tutorial_popup_bytecode(
            uint8_t* script,
            size_t script_size,
            const RuntimeTutorialPopupBypassProfile& profile) noexcept
            -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_final_world_combat_basics_ew01_script_size = 127098;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_callsite_offset = 0xC6CE;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_final_function_offset = 0xC6E4;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_latent_linkage_offset = 0xC6FB;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_latent_resume_offset = 0xC73A;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_tutorial_continuation_offset = 0xC748;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_bypass_continuation_offset = 0xCEA5;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_message_id_offset = 0xC714;
    inline constexpr size_t runtime_final_world_combat_basics_ew01_message_key_offset = 0xC718;

    inline constexpr size_t runtime_final_world_combat_basics_ew04_script_size = 134274;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_callsite_offset = 0xBF4C;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_final_function_offset = 0xBF62;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_latent_linkage_offset = 0xBF79;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_latent_resume_offset = 0xBFB8;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_tutorial_continuation_offset = 0xBFC6;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_bypass_continuation_offset = 0xC723;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_message_id_offset = 0xBF92;
    inline constexpr size_t runtime_final_world_combat_basics_ew04_message_key_offset = 0xBF96;

    inline constexpr uint8_t runtime_final_world_combat_basics_message_id = 31;
    inline constexpr std::string_view runtime_final_world_combat_basics_message_key =
            "kh3_level.ew.tut_0040";
    inline constexpr uint8_t runtime_final_world_combat_basics_continuation_opcode = 0x19;

    auto try_patch_runtime_final_world_combat_basics_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    // EW01 and EW04: preserve AP-owned controller abilities across vanilla setup.
    auto try_patch_runtime_final_world_ability_disables_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_he01_gameflow_script_size = 0x2901B;

    inline constexpr size_t runtime_olympus_he01_team_attacks_help_callsite_offset = 0x4E9;
    inline constexpr size_t runtime_olympus_he01_team_attacks_help_final_function_offset = 0x4FF;
    inline constexpr size_t runtime_olympus_he01_team_attacks_help_latent_linkage_offset = 0x516;
    inline constexpr size_t runtime_olympus_he01_team_attacks_help_latent_resume_offset = 0x534;
    inline constexpr size_t runtime_olympus_he01_team_attacks_help_continuation_offset = 0x53A;
    inline constexpr size_t runtime_olympus_he01_team_attacks_help_latent_resume_jump_target = 0x596;
    inline constexpr size_t runtime_olympus_he01_team_attacks_help_message_id_offset = 0x52F;
    inline constexpr uint8_t runtime_olympus_he01_team_attacks_help_message_id = 9;
    inline constexpr uint8_t runtime_olympus_he01_team_attacks_help_latent_resume_opcode = 0x07;
    inline constexpr uint8_t runtime_olympus_he01_team_attacks_help_continuation_opcode = 0x19;

    auto try_patch_runtime_olympus_he01_team_attacks_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_shotlock_bypass_entry_offset = 0xE082;
    inline constexpr size_t runtime_olympus_shotlock_tutorial_callsite_offset = 0xE0A7;
    inline constexpr size_t runtime_olympus_shotlock_final_function_offset = 0xE0BD;
    inline constexpr size_t runtime_olympus_shotlock_latent_linkage_offset = 0xE0D4;
    inline constexpr size_t runtime_olympus_shotlock_latent_resume_offset = 0xE116;
    inline constexpr size_t runtime_olympus_shotlock_tutorial_continuation_offset = 0xE124;
    inline constexpr size_t runtime_olympus_shotlock_bypass_continuation_offset = 0xE197;
    inline constexpr size_t runtime_olympus_shotlock_message_id_offset = 0xE0ED;
    inline constexpr size_t runtime_olympus_shotlock_message_key_offset = 0xE0F1;
    inline constexpr uint8_t runtime_olympus_shotlock_message_id = 11;
    inline constexpr std::string_view runtime_olympus_shotlock_message_key =
            "kh3_level.he.dialog_0060";
    inline constexpr uint8_t runtime_olympus_shotlock_continuation_opcode = 0x19;

    inline constexpr size_t runtime_olympus_airstepping_bypass_entry_offset = 0x28774;
    inline constexpr size_t runtime_olympus_airstepping_tutorial_callsite_offset = 0x28799;
    inline constexpr size_t runtime_olympus_airstepping_final_function_offset = 0x287AF;
    inline constexpr size_t runtime_olympus_airstepping_latent_linkage_offset = 0x287C6;
    inline constexpr size_t runtime_olympus_airstepping_latent_resume_offset = 0x104DC;
    inline constexpr size_t runtime_olympus_airstepping_tutorial_continuation_offset = 0x104EA;
    inline constexpr size_t runtime_olympus_airstepping_bypass_continuation_offset = 0x1055D;
    inline constexpr size_t runtime_olympus_airstepping_message_id_offset = 0x287DF;
    inline constexpr size_t runtime_olympus_airstepping_message_key_offset = 0x287E3;
    inline constexpr uint8_t runtime_olympus_airstepping_message_id = 12;
    inline constexpr std::string_view runtime_olympus_airstepping_message_key =
            "kh3_level.he.dialog_0070";
    inline constexpr uint8_t runtime_olympus_airstepping_continuation_opcode = 0x19;

    auto try_patch_runtime_olympus_shotlock_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_airstepping_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_early_tutorial_he02_script_size = 0x7116C;

    inline constexpr size_t runtime_olympus_casting_magic_bypass_entry_offset = 0x16414;
    inline constexpr size_t runtime_olympus_casting_magic_tutorial_callsite_offset = 0x16439;
    inline constexpr size_t runtime_olympus_casting_magic_final_function_offset = 0x1644F;
    inline constexpr size_t runtime_olympus_casting_magic_latent_linkage_offset = 0x16466;
    inline constexpr size_t runtime_olympus_casting_magic_latent_resume_offset = 0x164A8;
    inline constexpr size_t runtime_olympus_casting_magic_tutorial_continuation_offset = 0x164B6;
    inline constexpr size_t runtime_olympus_casting_magic_bypass_continuation_offset = 0x16E29;
    inline constexpr size_t runtime_olympus_casting_magic_message_id_offset = 0x1647F;
    inline constexpr size_t runtime_olympus_casting_magic_message_key_offset = 0x16483;
    inline constexpr uint8_t runtime_olympus_casting_magic_message_id = 34;
    inline constexpr std::string_view runtime_olympus_casting_magic_message_key =
            "kh3_level.he.dialog_0030";
    inline constexpr uint8_t runtime_olympus_casting_magic_tutorial_continuation_opcode = 0x1B;
    inline constexpr uint8_t runtime_olympus_casting_magic_bypass_continuation_opcode = 0x19;

    inline constexpr size_t runtime_olympus_free_running_bypass_entry_offset = 0x70849;
    inline constexpr size_t runtime_olympus_free_running_tutorial_callsite_offset = 0x7086E;
    inline constexpr size_t runtime_olympus_free_running_final_function_offset = 0x70884;
    inline constexpr size_t runtime_olympus_free_running_latent_linkage_offset = 0x7089B;
    inline constexpr size_t runtime_olympus_free_running_latent_resume_offset = 0x2ED4A;
    inline constexpr size_t runtime_olympus_free_running_tutorial_continuation_offset = 0x2ED58;
    inline constexpr size_t runtime_olympus_free_running_bypass_continuation_offset = 0x2EE50;
    inline constexpr size_t runtime_olympus_free_running_message_id_offset = 0x708B4;
    inline constexpr size_t runtime_olympus_free_running_message_key_offset = 0x708B8;
    inline constexpr uint8_t runtime_olympus_free_running_message_id = 25;
    inline constexpr std::string_view runtime_olympus_free_running_message_key =
            "kh3_level.he.dialog_0010";
    inline constexpr uint8_t runtime_olympus_free_running_continuation_opcode = 0x14;

    auto try_patch_runtime_olympus_casting_magic_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_free_running_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_flowmotion_help_callsite_offset = 0x1D4B7;
    inline constexpr size_t runtime_olympus_flowmotion_help_final_function_offset = 0x1D4CD;
    inline constexpr size_t runtime_olympus_flowmotion_help_latent_linkage_offset = 0x1D4E4;
    inline constexpr size_t runtime_olympus_flowmotion_help_continuation_offset = 0x1D502;
    inline constexpr size_t runtime_olympus_flowmotion_help_message_id_offset = 0x1D4FD;
    inline constexpr uint8_t runtime_olympus_flowmotion_help_message_id = 25;
    inline constexpr uint8_t runtime_olympus_flowmotion_help_continuation_opcode = 0x14;

    auto try_patch_runtime_olympus_flowmotion_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_attractions_help_callsite_offset = 0x2ECD4;
    inline constexpr size_t runtime_olympus_attractions_help_final_function_offset = 0x2ECEA;
    inline constexpr size_t runtime_olympus_attractions_help_latent_linkage_offset = 0x2ED01;
    inline constexpr size_t runtime_olympus_attractions_help_latent_resume_offset = 0x2EBD3;
    inline constexpr size_t runtime_olympus_attractions_help_followup_offset = 0x2EBD3;
    inline constexpr size_t runtime_olympus_attractions_help_bypass_continuation_offset = 0x2ED1E;
    inline constexpr size_t runtime_olympus_attractions_help_message_id_offset = 0x2ED1A;
    inline constexpr uint8_t runtime_olympus_attractions_help_message_id = 10;
    inline constexpr uint8_t runtime_olympus_attractions_help_followup_opcode = 0x14;
    inline constexpr uint8_t runtime_olympus_attractions_help_bypass_continuation_opcode = 0x4D;

    auto try_patch_runtime_olympus_attractions_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_he03_tutorial_script_size = 0x714BF;

    inline constexpr size_t runtime_olympus_flowmotion_tutorial_callsite_offset = 0xE33;
    inline constexpr size_t runtime_olympus_flowmotion_tutorial_final_function_offset = 0xE49;
    inline constexpr size_t runtime_olympus_flowmotion_tutorial_latent_linkage_offset = 0xE60;
    inline constexpr size_t runtime_olympus_flowmotion_tutorial_latent_resume_offset = 0xEA2;
    inline constexpr size_t runtime_olympus_flowmotion_tutorial_accepted_continuation_offset = 0xEB0;
    inline constexpr size_t runtime_olympus_flowmotion_tutorial_declined_continuation_offset = 0x138B;
    inline constexpr size_t runtime_olympus_flowmotion_tutorial_message_id_offset = 0xE79;
    inline constexpr size_t runtime_olympus_flowmotion_tutorial_message_key_offset = 0xE7D;
    inline constexpr uint8_t runtime_olympus_flowmotion_tutorial_message_id = 26;
    inline constexpr std::string_view runtime_olympus_flowmotion_tutorial_message_key =
            "kh3_level.he.dialog_0020";
    inline constexpr uint8_t runtime_olympus_flowmotion_tutorial_accepted_continuation_opcode = 0x1B;
    inline constexpr uint8_t runtime_olympus_flowmotion_tutorial_declined_continuation_opcode = 0x19;

    auto try_patch_runtime_olympus_flowmotion_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_team_attacks_help_callsite_offset = 0x3B75;
    inline constexpr size_t runtime_olympus_team_attacks_help_final_function_offset = 0x3B8B;
    inline constexpr size_t runtime_olympus_team_attacks_help_latent_linkage_offset = 0x3BA2;
    inline constexpr size_t runtime_olympus_team_attacks_help_latent_resume_offset = 0x3BC0;
    inline constexpr size_t runtime_olympus_team_attacks_help_fallthrough_offset = 0x3BC6;
    inline constexpr size_t runtime_olympus_team_attacks_help_bypass_continuation_offset = 0x3C16;
    inline constexpr size_t runtime_olympus_team_attacks_help_message_id_offset = 0x3BBB;
    inline constexpr uint8_t runtime_olympus_team_attacks_help_message_id = 9;
    inline constexpr uint8_t runtime_olympus_team_attacks_help_fallthrough_opcode = 0x14;
    inline constexpr uint8_t runtime_olympus_team_attacks_help_bypass_continuation_opcode = 0x19;

    auto try_patch_runtime_olympus_team_attacks_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_grand_magic_help_callsite_offset = 0x1A1C8;
    inline constexpr size_t runtime_olympus_grand_magic_help_final_function_offset = 0x1A1DE;
    inline constexpr size_t runtime_olympus_grand_magic_help_latent_linkage_offset = 0x1A1F5;
    inline constexpr size_t runtime_olympus_grand_magic_help_continuation_offset = 0x1A213;
    inline constexpr size_t runtime_olympus_grand_magic_help_message_id_offset = 0x1A20E;
    inline constexpr size_t runtime_olympus_grand_magic_help_continuation_target_offset = 0x1A804;
    inline constexpr uint8_t runtime_olympus_grand_magic_help_message_id = 15;
    inline constexpr uint8_t runtime_olympus_grand_magic_help_continuation_opcode = 0x07;

    auto try_patch_runtime_olympus_grand_magic_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_formchange_tutorial_callsite_offset = 0x1A219;
    inline constexpr size_t runtime_olympus_formchange_tutorial_final_function_offset = 0x1A22F;
    inline constexpr size_t runtime_olympus_formchange_tutorial_latent_linkage_offset = 0x1A246;
    inline constexpr size_t runtime_olympus_formchange_tutorial_latent_resume_offset = 0x1A288;
    inline constexpr size_t runtime_olympus_formchange_tutorial_accepted_continuation_offset = 0x1A296;
    inline constexpr size_t runtime_olympus_formchange_tutorial_declined_continuation_offset = 0x1A684;
    inline constexpr size_t runtime_olympus_formchange_tutorial_post_help_continuation_offset = 0x1A6DB;
    inline constexpr size_t runtime_olympus_formchange_tutorial_message_id_offset = 0x1A25F;
    inline constexpr size_t runtime_olympus_formchange_tutorial_message_key_offset = 0x1A263;
    inline constexpr uint8_t runtime_olympus_formchange_tutorial_message_id = 6;
    inline constexpr std::string_view runtime_olympus_formchange_tutorial_message_key =
            "kh3_level.he.dialog_0040";
    inline constexpr uint8_t runtime_olympus_formchange_tutorial_accepted_continuation_opcode = 0x19;
    inline constexpr uint8_t runtime_olympus_formchange_tutorial_declined_continuation_opcode = 0x07;
    inline constexpr uint8_t runtime_olympus_formchange_tutorial_post_help_continuation_opcode = 0x19;

    inline constexpr size_t runtime_olympus_formchange_followup_help_callsite_offset = 0x1A68A;
    inline constexpr size_t runtime_olympus_formchange_followup_help_final_function_offset = 0x1A6A0;
    inline constexpr size_t runtime_olympus_formchange_followup_help_latent_linkage_offset = 0x1A6B7;
    inline constexpr size_t runtime_olympus_formchange_followup_help_latent_resume_offset = 0x1A6D5;
    inline constexpr size_t runtime_olympus_formchange_followup_help_latent_resume_jump_target = 0x1A7A9;
    inline constexpr size_t runtime_olympus_formchange_followup_help_message_id_offset = 0x1A6D0;
    inline constexpr uint8_t runtime_olympus_formchange_followup_help_message_id = 125;

    auto try_patch_runtime_olympus_formchange_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_attractions_tutorial_callsite_offset = 0x233A2;
    inline constexpr size_t runtime_olympus_attractions_tutorial_final_function_offset = 0x233B8;
    inline constexpr size_t runtime_olympus_attractions_tutorial_latent_linkage_offset = 0x233CF;
    inline constexpr size_t runtime_olympus_attractions_tutorial_latent_resume_offset = 0x23411;
    inline constexpr size_t runtime_olympus_attractions_tutorial_accepted_continuation_offset = 0x2341F;
    inline constexpr size_t runtime_olympus_attractions_tutorial_declined_continuation_offset = 0x2380D;
    inline constexpr size_t runtime_olympus_attractions_tutorial_message_id_offset = 0x233E8;
    inline constexpr size_t runtime_olympus_attractions_tutorial_message_key_offset = 0x233EC;
    inline constexpr uint8_t runtime_olympus_attractions_tutorial_message_id = 10;
    inline constexpr std::string_view runtime_olympus_attractions_tutorial_message_key =
            "kh3_level.he.dialog_0050";
    inline constexpr uint8_t runtime_olympus_attractions_tutorial_accepted_continuation_opcode = 0x19;
    inline constexpr uint8_t runtime_olympus_attractions_tutorial_declined_continuation_opcode = 0x19;

    auto try_patch_runtime_olympus_attractions_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_save_points_callsite_offset = 0x9245;
    inline constexpr size_t runtime_olympus_save_points_final_function_offset = 0x925B;
    inline constexpr size_t runtime_olympus_save_points_latent_linkage_offset = 0x9272;
    inline constexpr size_t runtime_olympus_save_points_latent_resume_offset = 0x9290;
    inline constexpr size_t runtime_olympus_save_points_tutorial_continuation_offset = 0x9296;
    inline constexpr size_t runtime_olympus_save_points_bypass_continuation_offset = 0xA268;
    inline constexpr size_t runtime_olympus_save_points_message_id_offset = 0x928B;
    inline constexpr uint8_t runtime_olympus_save_points_message_id = 0;
    inline constexpr uint8_t runtime_olympus_save_points_tutorial_continuation_opcode = 0x07;
    inline constexpr uint8_t runtime_olympus_save_points_bypass_continuation_opcode = 0x19;

    auto try_patch_runtime_olympus_save_points_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_moogle_shop_callsite_offset = 0x929C;
    inline constexpr size_t runtime_olympus_moogle_shop_final_function_offset = 0x92B2;
    inline constexpr size_t runtime_olympus_moogle_shop_latent_linkage_offset = 0x92C9;
    inline constexpr size_t runtime_olympus_moogle_shop_latent_resume_offset = 0x92E7;
    inline constexpr size_t runtime_olympus_moogle_shop_tutorial_continuation_offset = 0x92ED;
    inline constexpr size_t runtime_olympus_moogle_shop_bypass_continuation_offset = 0xA20D;
    inline constexpr size_t runtime_olympus_moogle_shop_message_id_offset = 0x92E2;
    inline constexpr uint8_t runtime_olympus_moogle_shop_message_id = 1;
    inline constexpr uint8_t runtime_olympus_moogle_shop_continuation_opcode = 0x19;

    auto try_patch_runtime_olympus_moogle_shop_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    // Toy Box's standalone help calls all resume through an unconditional
    // EX_JumpIfNot(EX_True) post-dismiss path. Retarget only the validated
    // help call itself so cinematic cleanup, gimmick state, and story flags
    // remain intact. The separate Link patch suppresses Pixel Heartbinder's
    // direct vanilla grant and shares the 8-Bit Blast help continuation.
    inline constexpr size_t runtime_toy_box_aerial_recovery_script_size = 0x1C0CD;
    inline constexpr size_t runtime_toy_box_aerial_recovery_callsite_offset = 0x15AD6;
    inline constexpr size_t runtime_toy_box_aerial_recovery_final_function_offset = 0x15AEC;
    inline constexpr size_t runtime_toy_box_aerial_recovery_latent_linkage_offset = 0x15B03;
    inline constexpr size_t runtime_toy_box_aerial_recovery_latent_resume_offset = 0x14D32;
    inline constexpr size_t runtime_toy_box_aerial_recovery_latent_resume_jump_target = 0x15A5D;
    inline constexpr size_t runtime_toy_box_aerial_recovery_message_id_offset = 0x15B1C;
    inline constexpr uint8_t runtime_toy_box_aerial_recovery_message_id = 29;

    auto try_patch_runtime_toy_box_aerial_recovery_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_toy_box_gigas_script_size = 0x5C13;
    inline constexpr size_t runtime_toy_box_gigas_callsite_offset = 0x3CDC;
    inline constexpr size_t runtime_toy_box_gigas_final_function_offset = 0x3CF2;
    inline constexpr size_t runtime_toy_box_gigas_latent_linkage_offset = 0x3D09;
    inline constexpr size_t runtime_toy_box_gigas_latent_resume_offset = 0x1B23;
    inline constexpr size_t runtime_toy_box_gigas_latent_resume_jump_target = 0x1B86;
    inline constexpr size_t runtime_toy_box_gigas_message_id_offset = 0x3D22;
    inline constexpr uint8_t runtime_toy_box_gigas_message_id = 48;

    auto try_patch_runtime_toy_box_gigas_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_toy_box_mag_assemble_script_size = 0x2418;
    inline constexpr size_t runtime_toy_box_mag_assemble_callsite_offset = 0x131E;
    inline constexpr size_t runtime_toy_box_mag_assemble_final_function_offset = 0x1334;
    inline constexpr size_t runtime_toy_box_mag_assemble_latent_linkage_offset = 0x134B;
    inline constexpr size_t runtime_toy_box_mag_assemble_latent_resume_offset = 0xB26;
    inline constexpr size_t runtime_toy_box_mag_assemble_latent_resume_jump_target = 0x11D7;
    inline constexpr size_t runtime_toy_box_mag_assemble_message_id_offset = 0x1364;
    inline constexpr uint8_t runtime_toy_box_mag_assemble_message_id = 51;

    auto try_patch_runtime_toy_box_mag_assemble_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_toy_box_eight_bit_blast_script_size = 0x13DB0;
    inline constexpr size_t runtime_toy_box_eight_bit_blast_callsite_offset = 0xC108;
    inline constexpr size_t runtime_toy_box_eight_bit_blast_final_function_offset = 0xC11E;
    inline constexpr size_t runtime_toy_box_eight_bit_blast_latent_linkage_offset = 0xC135;
    inline constexpr size_t runtime_toy_box_eight_bit_blast_latent_resume_offset = 0x7FFA;
    inline constexpr size_t runtime_toy_box_eight_bit_blast_latent_resume_jump_target = 0x8116;
    inline constexpr size_t runtime_toy_box_eight_bit_blast_message_id_offset = 0xC14E;
    inline constexpr uint8_t runtime_toy_box_eight_bit_blast_message_id = 40;

    inline constexpr uint8_t runtime_toy_box_help_latent_resume_opcode = 0x07;

    auto try_patch_runtime_toy_box_eight_bit_blast_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    // The Caribbean's Sea Spectacle Game Help id 43 call is latent. Its stock
    // completion resumes at 0xE0 and enters TresEndCinematicMode at 0xE7.
    // Patch only the help call to that cleanup block: the 0.3-second delay and
    // TresStart/EndCinematicMode pair remain balanced so event-owned input and
    // camera locks are released. The CA_ISLAND_EVENT3 block at 0x10B is a
    // separate ubergraph entry and remains completely untouched.
    inline constexpr size_t runtime_caribbean_sea_spectacle_script_size = 0x1F77;
    inline constexpr size_t runtime_caribbean_sea_spectacle_callsite_offset = 0x62;
    inline constexpr size_t runtime_caribbean_sea_spectacle_bypass_entry_offset =
            runtime_caribbean_sea_spectacle_callsite_offset;
    inline constexpr size_t runtime_caribbean_sea_spectacle_final_function_offset = 0x78;
    inline constexpr size_t runtime_caribbean_sea_spectacle_latent_linkage_offset = 0x8F;
    inline constexpr size_t runtime_caribbean_sea_spectacle_latent_resume_offset = 0xE0;
    inline constexpr size_t runtime_caribbean_sea_spectacle_post_dismiss_offset = 0xE7;
    inline constexpr size_t runtime_caribbean_sea_spectacle_tutorial_continuation_offset =
            runtime_caribbean_sea_spectacle_post_dismiss_offset;
    inline constexpr size_t runtime_caribbean_sea_spectacle_message_id_offset = 0xA8;
    inline constexpr uint8_t runtime_caribbean_sea_spectacle_message_id = 43;
    inline constexpr uint8_t runtime_caribbean_sea_spectacle_context_opcode = 0x19;

    // Stock presentation/cleanup anchors retained by the popup-only patch.
    inline constexpr size_t runtime_caribbean_sea_spectacle_delay_callsite_offset = 0xF;
    inline constexpr size_t runtime_caribbean_sea_spectacle_delay_final_function_offset = 0x25;
    inline constexpr size_t runtime_caribbean_sea_spectacle_start_cinematic_callsite_offset = 0xAD;
    inline constexpr size_t runtime_caribbean_sea_spectacle_start_cinematic_final_function_offset = 0xC3;
    inline constexpr size_t runtime_caribbean_sea_spectacle_end_cinematic_final_function_offset = 0xFD;
    inline constexpr size_t runtime_caribbean_sea_spectacle_island_event3_entry_offset = 0x10B;

    auto try_patch_runtime_caribbean_sea_spectacle_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    // Twilight Town's Taking Photos, Lucky Emblems, Links, and Meow Wow
    // Balloon panels are presentation-only. Retarget only their exact Game
    // Help calls to the original post-dismiss continuations so the surrounding
    // story, autosave, cinematic, and special-battle flow remains intact. The
    // Taking Photos bypass still runs EndCinematicMode and the persistent
    // "Using the Gummiphone Camera" Game Help flag write.
    inline constexpr size_t runtime_twilight_town_tutorial_script_size = 0x65CA7;

    inline constexpr size_t runtime_twilight_town_taking_photos_callsite_offset = 0xEBAA;
    inline constexpr size_t runtime_twilight_town_taking_photos_final_function_offset = 0xEBC0;
    inline constexpr size_t runtime_twilight_town_taking_photos_latent_linkage_offset = 0xEBD7;
    inline constexpr size_t runtime_twilight_town_taking_photos_latent_resume_offset = 0xEBF5;
    inline constexpr size_t runtime_twilight_town_taking_photos_bypass_continuation_offset = 0x10CE6;
    inline constexpr size_t runtime_twilight_town_taking_photos_message_id_offset = 0xEBF0;
    inline constexpr uint8_t runtime_twilight_town_taking_photos_message_id = 3;

    auto try_patch_runtime_twilight_town_taking_photos_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_twilight_town_lucky_emblems_callsite_offset = 0xEC24;
    inline constexpr size_t runtime_twilight_town_lucky_emblems_final_function_offset = 0xEC3A;
    inline constexpr size_t runtime_twilight_town_lucky_emblems_latent_linkage_offset = 0xEC51;
    inline constexpr size_t runtime_twilight_town_lucky_emblems_latent_resume_offset = 0xEC6F;
    inline constexpr size_t runtime_twilight_town_lucky_emblems_bypass_continuation_offset = 0x10C8B;
    inline constexpr size_t runtime_twilight_town_lucky_emblems_message_id_offset = 0xEC6A;
    inline constexpr uint8_t runtime_twilight_town_lucky_emblems_message_id = 5;

    auto try_patch_runtime_twilight_town_lucky_emblems_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_twilight_town_links_callsite_offset = 0x24166;
    inline constexpr size_t runtime_twilight_town_links_final_function_offset = 0x2417C;
    inline constexpr size_t runtime_twilight_town_links_latent_linkage_offset = 0x24193;
    inline constexpr size_t runtime_twilight_town_links_latent_resume_offset = 0x241B1;
    inline constexpr size_t runtime_twilight_town_links_latent_resume_jump_target = 0x24495;
    inline constexpr size_t runtime_twilight_town_links_message_id_offset = 0x241AC;
    inline constexpr uint8_t runtime_twilight_town_links_message_id = 8;

    auto try_patch_runtime_twilight_town_links_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_twilight_town_meow_wow_callsite_offset = 0x241BD;
    inline constexpr size_t runtime_twilight_town_meow_wow_final_function_offset = 0x241D3;
    inline constexpr size_t runtime_twilight_town_meow_wow_latent_linkage_offset = 0x241EA;
    inline constexpr size_t runtime_twilight_town_meow_wow_latent_resume_offset = 0x24208;
    inline constexpr size_t runtime_twilight_town_meow_wow_latent_resume_jump_target = 0x2443A;
    inline constexpr size_t runtime_twilight_town_meow_wow_message_id_offset = 0x24203;
    inline constexpr uint8_t runtime_twilight_town_meow_wow_message_id = 39;

    inline constexpr uint8_t runtime_twilight_town_help_latent_resume_opcode = 0x07;

    auto try_patch_runtime_twilight_town_meow_wow_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    struct RuntimeAbilitySetBypassProfile
    {
        size_t script_size{};
        size_t callsite_offset{};
        size_t virtual_function_offset{};
        size_t value_offset{};
        uint8_t value_opcode{};
        size_t continuation_offset{};
        uint8_t continuation_opcode{};
    };

    auto try_patch_runtime_ability_set_bytecode(
            uint8_t* script,
            size_t script_size,
            const RuntimeAbilitySetBypassProfile& profile) noexcept
            -> RuntimeTutorialPopupBypassResult;

    inline constexpr size_t runtime_olympus_he01_attraction_flow_enable_callsite_offset = 0x2641C;
    inline constexpr size_t runtime_olympus_he01_attraction_flow_enable_virtual_function_offset = 0x26432;
    inline constexpr size_t runtime_olympus_he01_attraction_flow_enable_value_offset = 0x2643F;
    inline constexpr size_t runtime_olympus_he01_attraction_flow_enable_continuation_offset = 0x26441;
    inline constexpr uint8_t runtime_olympus_he01_attraction_flow_enable_continuation_opcode = 0x06;

    inline constexpr size_t runtime_olympus_he02_attraction_flow_enable_callsite_offset = 0x6F597;
    inline constexpr size_t runtime_olympus_he02_attraction_flow_enable_virtual_function_offset = 0x6F5AD;
    inline constexpr size_t runtime_olympus_he02_attraction_flow_enable_value_offset = 0x6F5BA;
    inline constexpr size_t runtime_olympus_he02_attraction_flow_enable_continuation_offset = 0x6F5BC;
    inline constexpr uint8_t runtime_olympus_he02_attraction_flow_enable_continuation_opcode = 0x06;

    inline constexpr size_t runtime_olympus_he03_attraction_flow_tutorial_enable_callsite_offset = 0x2337D;
    inline constexpr size_t runtime_olympus_he03_attraction_flow_tutorial_enable_virtual_function_offset = 0x23393;
    inline constexpr size_t runtime_olympus_he03_attraction_flow_tutorial_enable_value_offset = 0x233A0;

    inline constexpr size_t runtime_olympus_he03_attraction_flow_restore_callsite_offset = 0x27FE7;
    inline constexpr size_t runtime_olympus_he03_attraction_flow_restore_virtual_function_offset = 0x27FFD;
    inline constexpr size_t runtime_olympus_he03_attraction_flow_restore_value_offset = 0x2800A;
    inline constexpr size_t runtime_olympus_he03_attraction_flow_restore_continuation_offset = 0x2800C;
    inline constexpr uint8_t runtime_olympus_he03_attraction_flow_restore_continuation_opcode = 0x5F;

    inline constexpr size_t runtime_olympus_he01_style_change_enable_callsite_offset = 0x26471;
    inline constexpr size_t runtime_olympus_he01_style_change_enable_virtual_function_offset = 0x26487;
    inline constexpr size_t runtime_olympus_he01_style_change_enable_value_offset = 0x26494;
    inline constexpr size_t runtime_olympus_he01_style_change_enable_continuation_offset = 0x26496;
    inline constexpr uint8_t runtime_olympus_he01_style_change_enable_continuation_opcode = 0x06;

    inline constexpr size_t runtime_olympus_he02_style_change_enable_callsite_offset = 0x6F5EC;
    inline constexpr size_t runtime_olympus_he02_style_change_enable_virtual_function_offset = 0x6F602;
    inline constexpr size_t runtime_olympus_he02_style_change_enable_value_offset = 0x6F60F;
    inline constexpr size_t runtime_olympus_he02_style_change_enable_continuation_offset = 0x6F611;
    inline constexpr uint8_t runtime_olympus_he02_style_change_enable_continuation_opcode = 0x06;

    inline constexpr size_t runtime_olympus_he03_style_change_tutorial_enable_callsite_offset = 0x1A19D;
    inline constexpr size_t runtime_olympus_he03_style_change_tutorial_enable_virtual_function_offset = 0x1A1B3;
    inline constexpr size_t runtime_olympus_he03_style_change_tutorial_enable_value_offset = 0x1A1C0;
    inline constexpr size_t runtime_olympus_he03_style_change_tutorial_enable_continuation_offset = 0x1A1C2;
    inline constexpr uint8_t runtime_olympus_he03_style_change_tutorial_enable_continuation_opcode = 0x07;

    inline constexpr size_t runtime_olympus_he03_style_change_restore_callsite_offset = 0x27F97;
    inline constexpr size_t runtime_olympus_he03_style_change_restore_virtual_function_offset = 0x27FAD;
    inline constexpr size_t runtime_olympus_he03_style_change_restore_value_offset = 0x27FBA;
    inline constexpr size_t runtime_olympus_he03_style_change_restore_continuation_offset = 0x27FBC;
    inline constexpr uint8_t runtime_olympus_he03_style_change_restore_continuation_opcode = 0x5F;

    auto try_patch_runtime_olympus_he01_attraction_flow_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he02_attraction_flow_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he03_attraction_flow_tutorial_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he03_attraction_flow_restore_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he01_style_change_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he02_style_change_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he03_style_change_tutorial_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he03_style_change_restore_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    // Suppress every validated BP_SetAbility*(true/false) call owned by the
    // tutorial flows. Shotlock and Airstepping in he_01 and the two setters at
    // he_02 offsets 0x16414 and 0x70849 are already skipped by their surrounding
    // tutorial-entry jumps and therefore must not be patched a second time.
    auto try_patch_runtime_olympus_he01_tutorial_ability_sets_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he02_tutorial_ability_sets_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;

    auto try_patch_runtime_olympus_he03_tutorial_ability_sets_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult;
}
