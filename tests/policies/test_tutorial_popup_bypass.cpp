#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"
#include "kh3ap/features/tutorials/informational_help_bypass.h"

#include <algorithm>
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using PatchFunction = kh3ap::RuntimeTutorialPopupBypassResult (*)(
            uint8_t*,
            size_t) noexcept;

    struct TestFollowupHelpAnchor
    {
        size_t callsite_offset{};
        size_t final_function_offset{};
        size_t latent_linkage_offset{};
        size_t latent_resume_offset{};
        size_t latent_resume_jump_target{};
        size_t message_id_offset{};
        uint8_t message_id{};
    };

    struct TestProfile
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
        PatchFunction patch{};
        bool latent_resume_has_no_bypass_branch{false};
        size_t latent_resume_direct_jump_target{};
        size_t patched_bypass_continuation_offset{};
        uint8_t patched_bypass_continuation_opcode{};
        TestFollowupHelpAnchor bypass_followup_help{};
    };

    struct TestAbilitySetProfile
    {
        size_t script_size{};
        size_t callsite_offset{};
        size_t virtual_function_offset{};
        size_t value_offset{};
        uint8_t value_opcode{};
        size_t continuation_offset{};
        uint8_t continuation_opcode{};
        PatchFunction patch{};
    };

    constexpr TestProfile ew01_profile{
            kh3ap::runtime_final_world_combat_basics_ew01_script_size,
            kh3ap::runtime_final_world_combat_basics_ew01_callsite_offset,
            0x20,
            kh3ap::runtime_final_world_combat_basics_ew01_callsite_offset,
            kh3ap::runtime_final_world_combat_basics_ew01_final_function_offset,
            kh3ap::runtime_final_world_combat_basics_ew01_latent_linkage_offset,
            kh3ap::runtime_final_world_combat_basics_ew01_latent_resume_offset,
            kh3ap::runtime_final_world_combat_basics_ew01_tutorial_continuation_offset,
            kh3ap::runtime_final_world_combat_basics_continuation_opcode,
            kh3ap::runtime_final_world_combat_basics_ew01_bypass_continuation_offset,
            kh3ap::runtime_final_world_combat_basics_continuation_opcode,
            kh3ap::runtime_final_world_combat_basics_ew01_message_id_offset,
            kh3ap::runtime_final_world_combat_basics_message_id,
            kh3ap::runtime_final_world_combat_basics_ew01_message_key_offset,
            kh3ap::runtime_final_world_combat_basics_message_key,
            &kh3ap::try_patch_runtime_final_world_combat_basics_tutorial_bytecode,
    };

    constexpr TestProfile ew04_profile{
            kh3ap::runtime_final_world_combat_basics_ew04_script_size,
            kh3ap::runtime_final_world_combat_basics_ew04_callsite_offset,
            0x20,
            kh3ap::runtime_final_world_combat_basics_ew04_callsite_offset,
            kh3ap::runtime_final_world_combat_basics_ew04_final_function_offset,
            kh3ap::runtime_final_world_combat_basics_ew04_latent_linkage_offset,
            kh3ap::runtime_final_world_combat_basics_ew04_latent_resume_offset,
            kh3ap::runtime_final_world_combat_basics_ew04_tutorial_continuation_offset,
            kh3ap::runtime_final_world_combat_basics_continuation_opcode,
            kh3ap::runtime_final_world_combat_basics_ew04_bypass_continuation_offset,
            kh3ap::runtime_final_world_combat_basics_continuation_opcode,
            kh3ap::runtime_final_world_combat_basics_ew04_message_id_offset,
            kh3ap::runtime_final_world_combat_basics_message_id,
            kh3ap::runtime_final_world_combat_basics_ew04_message_key_offset,
            kh3ap::runtime_final_world_combat_basics_message_key,
            &kh3ap::try_patch_runtime_final_world_combat_basics_tutorial_bytecode,
    };

    constexpr TestProfile he01_team_attacks_help_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            kh3ap::runtime_olympus_he01_team_attacks_help_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_he01_team_attacks_help_callsite_offset,
            kh3ap::runtime_olympus_he01_team_attacks_help_final_function_offset,
            kh3ap::runtime_olympus_he01_team_attacks_help_latent_linkage_offset,
            kh3ap::runtime_olympus_he01_team_attacks_help_latent_resume_offset,
            kh3ap::runtime_olympus_he01_team_attacks_help_latent_resume_offset,
            kh3ap::runtime_olympus_he01_team_attacks_help_latent_resume_opcode,
            kh3ap::runtime_olympus_he01_team_attacks_help_continuation_offset,
            kh3ap::runtime_olympus_he01_team_attacks_help_continuation_opcode,
            kh3ap::runtime_olympus_he01_team_attacks_help_message_id_offset,
            kh3ap::runtime_olympus_he01_team_attacks_help_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_olympus_he01_team_attacks_help_bytecode,
            true,
            kh3ap::runtime_olympus_he01_team_attacks_help_latent_resume_jump_target,
    };

    constexpr TestProfile shotlock_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            kh3ap::runtime_olympus_shotlock_bypass_entry_offset,
            0x00,
            kh3ap::runtime_olympus_shotlock_tutorial_callsite_offset,
            kh3ap::runtime_olympus_shotlock_final_function_offset,
            kh3ap::runtime_olympus_shotlock_latent_linkage_offset,
            kh3ap::runtime_olympus_shotlock_latent_resume_offset,
            kh3ap::runtime_olympus_shotlock_tutorial_continuation_offset,
            kh3ap::runtime_olympus_shotlock_continuation_opcode,
            kh3ap::runtime_olympus_shotlock_bypass_continuation_offset,
            kh3ap::runtime_olympus_shotlock_continuation_opcode,
            kh3ap::runtime_olympus_shotlock_message_id_offset,
            kh3ap::runtime_olympus_shotlock_message_id,
            kh3ap::runtime_olympus_shotlock_message_key_offset,
            kh3ap::runtime_olympus_shotlock_message_key,
            &kh3ap::try_patch_runtime_olympus_shotlock_tutorial_bytecode,
    };

    constexpr TestProfile airstepping_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            kh3ap::runtime_olympus_airstepping_bypass_entry_offset,
            0x00,
            kh3ap::runtime_olympus_airstepping_tutorial_callsite_offset,
            kh3ap::runtime_olympus_airstepping_final_function_offset,
            kh3ap::runtime_olympus_airstepping_latent_linkage_offset,
            kh3ap::runtime_olympus_airstepping_latent_resume_offset,
            kh3ap::runtime_olympus_airstepping_tutorial_continuation_offset,
            kh3ap::runtime_olympus_airstepping_continuation_opcode,
            kh3ap::runtime_olympus_airstepping_bypass_continuation_offset,
            kh3ap::runtime_olympus_airstepping_continuation_opcode,
            kh3ap::runtime_olympus_airstepping_message_id_offset,
            kh3ap::runtime_olympus_airstepping_message_id,
            kh3ap::runtime_olympus_airstepping_message_key_offset,
            kh3ap::runtime_olympus_airstepping_message_key,
            &kh3ap::try_patch_runtime_olympus_airstepping_tutorial_bytecode,
    };

    constexpr TestProfile casting_magic_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            kh3ap::runtime_olympus_casting_magic_bypass_entry_offset,
            0x00,
            kh3ap::runtime_olympus_casting_magic_tutorial_callsite_offset,
            kh3ap::runtime_olympus_casting_magic_final_function_offset,
            kh3ap::runtime_olympus_casting_magic_latent_linkage_offset,
            kh3ap::runtime_olympus_casting_magic_latent_resume_offset,
            kh3ap::runtime_olympus_casting_magic_tutorial_continuation_offset,
            kh3ap::runtime_olympus_casting_magic_tutorial_continuation_opcode,
            kh3ap::runtime_olympus_casting_magic_bypass_continuation_offset,
            kh3ap::runtime_olympus_casting_magic_bypass_continuation_opcode,
            kh3ap::runtime_olympus_casting_magic_message_id_offset,
            kh3ap::runtime_olympus_casting_magic_message_id,
            kh3ap::runtime_olympus_casting_magic_message_key_offset,
            kh3ap::runtime_olympus_casting_magic_message_key,
            &kh3ap::try_patch_runtime_olympus_casting_magic_tutorial_bytecode,
    };

    constexpr TestProfile free_running_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            kh3ap::runtime_olympus_free_running_bypass_entry_offset,
            0x00,
            kh3ap::runtime_olympus_free_running_tutorial_callsite_offset,
            kh3ap::runtime_olympus_free_running_final_function_offset,
            kh3ap::runtime_olympus_free_running_latent_linkage_offset,
            kh3ap::runtime_olympus_free_running_latent_resume_offset,
            kh3ap::runtime_olympus_free_running_tutorial_continuation_offset,
            kh3ap::runtime_olympus_free_running_continuation_opcode,
            kh3ap::runtime_olympus_free_running_bypass_continuation_offset,
            kh3ap::runtime_olympus_free_running_continuation_opcode,
            kh3ap::runtime_olympus_free_running_message_id_offset,
            kh3ap::runtime_olympus_free_running_message_id,
            kh3ap::runtime_olympus_free_running_message_key_offset,
            kh3ap::runtime_olympus_free_running_message_key,
            &kh3ap::try_patch_runtime_olympus_free_running_tutorial_bytecode,
    };

    constexpr TestProfile flowmotion_help_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            kh3ap::runtime_olympus_flowmotion_help_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_flowmotion_help_callsite_offset,
            kh3ap::runtime_olympus_flowmotion_help_final_function_offset,
            kh3ap::runtime_olympus_flowmotion_help_latent_linkage_offset,
            kh3ap::runtime_olympus_flowmotion_help_continuation_offset,
            kh3ap::runtime_olympus_flowmotion_help_continuation_offset,
            kh3ap::runtime_olympus_flowmotion_help_continuation_opcode,
            kh3ap::runtime_olympus_flowmotion_help_continuation_offset,
            kh3ap::runtime_olympus_flowmotion_help_continuation_opcode,
            kh3ap::runtime_olympus_flowmotion_help_message_id_offset,
            kh3ap::runtime_olympus_flowmotion_help_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_olympus_flowmotion_help_bytecode,
            true,
    };

    constexpr TestProfile attractions_help_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            kh3ap::runtime_olympus_attractions_help_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_attractions_help_callsite_offset,
            kh3ap::runtime_olympus_attractions_help_final_function_offset,
            kh3ap::runtime_olympus_attractions_help_latent_linkage_offset,
            kh3ap::runtime_olympus_attractions_help_latent_resume_offset,
            kh3ap::runtime_olympus_attractions_help_followup_offset,
            kh3ap::runtime_olympus_attractions_help_followup_opcode,
            kh3ap::runtime_olympus_attractions_help_bypass_continuation_offset,
            kh3ap::runtime_olympus_attractions_help_bypass_continuation_opcode,
            kh3ap::runtime_olympus_attractions_help_message_id_offset,
            kh3ap::runtime_olympus_attractions_help_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_olympus_attractions_help_bytecode,
            true,
    };

    constexpr TestProfile flowmotion_tutorial_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_flowmotion_tutorial_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_flowmotion_tutorial_callsite_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_final_function_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_latent_linkage_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_latent_resume_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_accepted_continuation_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_accepted_continuation_opcode,
            kh3ap::runtime_olympus_flowmotion_tutorial_declined_continuation_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_declined_continuation_opcode,
            kh3ap::runtime_olympus_flowmotion_tutorial_message_id_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_message_id,
            kh3ap::runtime_olympus_flowmotion_tutorial_message_key_offset,
            kh3ap::runtime_olympus_flowmotion_tutorial_message_key,
            &kh3ap::try_patch_runtime_olympus_flowmotion_tutorial_bytecode,
    };

    constexpr TestProfile team_attacks_help_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_team_attacks_help_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_team_attacks_help_callsite_offset,
            kh3ap::runtime_olympus_team_attacks_help_final_function_offset,
            kh3ap::runtime_olympus_team_attacks_help_latent_linkage_offset,
            kh3ap::runtime_olympus_team_attacks_help_latent_resume_offset,
            kh3ap::runtime_olympus_team_attacks_help_fallthrough_offset,
            kh3ap::runtime_olympus_team_attacks_help_fallthrough_opcode,
            kh3ap::runtime_olympus_team_attacks_help_bypass_continuation_offset,
            kh3ap::runtime_olympus_team_attacks_help_bypass_continuation_opcode,
            kh3ap::runtime_olympus_team_attacks_help_message_id_offset,
            kh3ap::runtime_olympus_team_attacks_help_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_olympus_team_attacks_help_bytecode,
    };

    constexpr TestProfile grand_magic_help_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_grand_magic_help_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_grand_magic_help_callsite_offset,
            kh3ap::runtime_olympus_grand_magic_help_final_function_offset,
            kh3ap::runtime_olympus_grand_magic_help_latent_linkage_offset,
            kh3ap::runtime_olympus_grand_magic_help_continuation_offset,
            kh3ap::runtime_olympus_grand_magic_help_continuation_offset,
            kh3ap::runtime_olympus_grand_magic_help_continuation_opcode,
            kh3ap::runtime_olympus_grand_magic_help_continuation_offset,
            kh3ap::runtime_olympus_grand_magic_help_continuation_opcode,
            kh3ap::runtime_olympus_grand_magic_help_message_id_offset,
            kh3ap::runtime_olympus_grand_magic_help_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_olympus_grand_magic_help_bytecode,
            true,
            kh3ap::runtime_olympus_grand_magic_help_continuation_target_offset,
    };

    constexpr TestProfile formchange_tutorial_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_formchange_tutorial_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_formchange_tutorial_callsite_offset,
            kh3ap::runtime_olympus_formchange_tutorial_final_function_offset,
            kh3ap::runtime_olympus_formchange_tutorial_latent_linkage_offset,
            kh3ap::runtime_olympus_formchange_tutorial_latent_resume_offset,
            kh3ap::runtime_olympus_formchange_tutorial_accepted_continuation_offset,
            kh3ap::runtime_olympus_formchange_tutorial_accepted_continuation_opcode,
            kh3ap::runtime_olympus_formchange_tutorial_declined_continuation_offset,
            kh3ap::runtime_olympus_formchange_tutorial_declined_continuation_opcode,
            kh3ap::runtime_olympus_formchange_tutorial_message_id_offset,
            kh3ap::runtime_olympus_formchange_tutorial_message_id,
            kh3ap::runtime_olympus_formchange_tutorial_message_key_offset,
            kh3ap::runtime_olympus_formchange_tutorial_message_key,
            &kh3ap::try_patch_runtime_olympus_formchange_tutorial_bytecode,
            false,
            0,
            kh3ap::runtime_olympus_formchange_tutorial_post_help_continuation_offset,
            kh3ap::runtime_olympus_formchange_tutorial_post_help_continuation_opcode,
            {
                    kh3ap::runtime_olympus_formchange_followup_help_callsite_offset,
                    kh3ap::runtime_olympus_formchange_followup_help_final_function_offset,
                    kh3ap::runtime_olympus_formchange_followup_help_latent_linkage_offset,
                    kh3ap::runtime_olympus_formchange_followup_help_latent_resume_offset,
                    kh3ap::runtime_olympus_formchange_followup_help_latent_resume_jump_target,
                    kh3ap::runtime_olympus_formchange_followup_help_message_id_offset,
                    kh3ap::runtime_olympus_formchange_followup_help_message_id,
            },
    };

    constexpr TestProfile attractions_tutorial_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_attractions_tutorial_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_attractions_tutorial_callsite_offset,
            kh3ap::runtime_olympus_attractions_tutorial_final_function_offset,
            kh3ap::runtime_olympus_attractions_tutorial_latent_linkage_offset,
            kh3ap::runtime_olympus_attractions_tutorial_latent_resume_offset,
            kh3ap::runtime_olympus_attractions_tutorial_accepted_continuation_offset,
            kh3ap::runtime_olympus_attractions_tutorial_accepted_continuation_opcode,
            kh3ap::runtime_olympus_attractions_tutorial_declined_continuation_offset,
            kh3ap::runtime_olympus_attractions_tutorial_declined_continuation_opcode,
            kh3ap::runtime_olympus_attractions_tutorial_message_id_offset,
            kh3ap::runtime_olympus_attractions_tutorial_message_id,
            kh3ap::runtime_olympus_attractions_tutorial_message_key_offset,
            kh3ap::runtime_olympus_attractions_tutorial_message_key,
            &kh3ap::try_patch_runtime_olympus_attractions_tutorial_bytecode,
    };

    constexpr TestProfile save_points_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_save_points_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_save_points_callsite_offset,
            kh3ap::runtime_olympus_save_points_final_function_offset,
            kh3ap::runtime_olympus_save_points_latent_linkage_offset,
            kh3ap::runtime_olympus_save_points_latent_resume_offset,
            kh3ap::runtime_olympus_save_points_tutorial_continuation_offset,
            kh3ap::runtime_olympus_save_points_tutorial_continuation_opcode,
            kh3ap::runtime_olympus_save_points_bypass_continuation_offset,
            kh3ap::runtime_olympus_save_points_bypass_continuation_opcode,
            kh3ap::runtime_olympus_save_points_message_id_offset,
            kh3ap::runtime_olympus_save_points_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_olympus_save_points_tutorial_bytecode,
    };

    constexpr TestProfile moogle_shop_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_moogle_shop_callsite_offset,
            0x20,
            kh3ap::runtime_olympus_moogle_shop_callsite_offset,
            kh3ap::runtime_olympus_moogle_shop_final_function_offset,
            kh3ap::runtime_olympus_moogle_shop_latent_linkage_offset,
            kh3ap::runtime_olympus_moogle_shop_latent_resume_offset,
            kh3ap::runtime_olympus_moogle_shop_tutorial_continuation_offset,
            kh3ap::runtime_olympus_moogle_shop_continuation_opcode,
            kh3ap::runtime_olympus_moogle_shop_bypass_continuation_offset,
            kh3ap::runtime_olympus_moogle_shop_continuation_opcode,
            kh3ap::runtime_olympus_moogle_shop_message_id_offset,
            kh3ap::runtime_olympus_moogle_shop_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_olympus_moogle_shop_tutorial_bytecode,
    };

    constexpr TestProfile toy_box_aerial_recovery_profile{
            kh3ap::runtime_toy_box_aerial_recovery_script_size,
            kh3ap::runtime_toy_box_aerial_recovery_callsite_offset,
            0x20,
            kh3ap::runtime_toy_box_aerial_recovery_callsite_offset,
            kh3ap::runtime_toy_box_aerial_recovery_final_function_offset,
            kh3ap::runtime_toy_box_aerial_recovery_latent_linkage_offset,
            kh3ap::runtime_toy_box_aerial_recovery_latent_resume_offset,
            kh3ap::runtime_toy_box_aerial_recovery_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_aerial_recovery_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_aerial_recovery_message_id_offset,
            kh3ap::runtime_toy_box_aerial_recovery_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_toy_box_aerial_recovery_help_bytecode,
            true,
            kh3ap::runtime_toy_box_aerial_recovery_latent_resume_jump_target,
    };

    constexpr TestProfile toy_box_gigas_profile{
            kh3ap::runtime_toy_box_gigas_script_size,
            kh3ap::runtime_toy_box_gigas_callsite_offset,
            0x20,
            kh3ap::runtime_toy_box_gigas_callsite_offset,
            kh3ap::runtime_toy_box_gigas_final_function_offset,
            kh3ap::runtime_toy_box_gigas_latent_linkage_offset,
            kh3ap::runtime_toy_box_gigas_latent_resume_offset,
            kh3ap::runtime_toy_box_gigas_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_gigas_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_gigas_message_id_offset,
            kh3ap::runtime_toy_box_gigas_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_toy_box_gigas_help_bytecode,
            true,
            kh3ap::runtime_toy_box_gigas_latent_resume_jump_target,
    };

    constexpr TestProfile toy_box_mag_assemble_profile{
            kh3ap::runtime_toy_box_mag_assemble_script_size,
            kh3ap::runtime_toy_box_mag_assemble_callsite_offset,
            0x20,
            kh3ap::runtime_toy_box_mag_assemble_callsite_offset,
            kh3ap::runtime_toy_box_mag_assemble_final_function_offset,
            kh3ap::runtime_toy_box_mag_assemble_latent_linkage_offset,
            kh3ap::runtime_toy_box_mag_assemble_latent_resume_offset,
            kh3ap::runtime_toy_box_mag_assemble_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_mag_assemble_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_mag_assemble_message_id_offset,
            kh3ap::runtime_toy_box_mag_assemble_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_toy_box_mag_assemble_help_bytecode,
            true,
            kh3ap::runtime_toy_box_mag_assemble_latent_resume_jump_target,
    };

    constexpr TestProfile toy_box_eight_bit_blast_profile{
            kh3ap::runtime_toy_box_eight_bit_blast_script_size,
            kh3ap::runtime_toy_box_eight_bit_blast_callsite_offset,
            0x20,
            kh3ap::runtime_toy_box_eight_bit_blast_callsite_offset,
            kh3ap::runtime_toy_box_eight_bit_blast_final_function_offset,
            kh3ap::runtime_toy_box_eight_bit_blast_latent_linkage_offset,
            kh3ap::runtime_toy_box_eight_bit_blast_latent_resume_offset,
            kh3ap::runtime_toy_box_eight_bit_blast_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_eight_bit_blast_latent_resume_offset,
            kh3ap::runtime_toy_box_help_latent_resume_opcode,
            kh3ap::runtime_toy_box_eight_bit_blast_message_id_offset,
            kh3ap::runtime_toy_box_eight_bit_blast_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_toy_box_eight_bit_blast_help_bytecode,
            true,
            kh3ap::runtime_toy_box_eight_bit_blast_latent_resume_jump_target,
    };

    constexpr TestProfile caribbean_sea_spectacle_profile{
            kh3ap::runtime_caribbean_sea_spectacle_script_size,
            kh3ap::runtime_caribbean_sea_spectacle_bypass_entry_offset,
            0x20,
            kh3ap::runtime_caribbean_sea_spectacle_callsite_offset,
            kh3ap::runtime_caribbean_sea_spectacle_final_function_offset,
            kh3ap::runtime_caribbean_sea_spectacle_latent_linkage_offset,
            kh3ap::runtime_caribbean_sea_spectacle_latent_resume_offset,
            kh3ap::runtime_caribbean_sea_spectacle_tutorial_continuation_offset,
            kh3ap::runtime_caribbean_sea_spectacle_context_opcode,
            kh3ap::runtime_caribbean_sea_spectacle_tutorial_continuation_offset,
            kh3ap::runtime_caribbean_sea_spectacle_context_opcode,
            kh3ap::runtime_caribbean_sea_spectacle_message_id_offset,
            kh3ap::runtime_caribbean_sea_spectacle_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_caribbean_sea_spectacle_help_bytecode,
    };

    constexpr TestProfile twilight_town_taking_photos_profile{
            kh3ap::runtime_twilight_town_tutorial_script_size,
            kh3ap::runtime_twilight_town_taking_photos_callsite_offset,
            0x20,
            kh3ap::runtime_twilight_town_taking_photos_callsite_offset,
            kh3ap::runtime_twilight_town_taking_photos_final_function_offset,
            kh3ap::runtime_twilight_town_taking_photos_latent_linkage_offset,
            kh3ap::runtime_twilight_town_taking_photos_latent_resume_offset,
            kh3ap::runtime_twilight_town_taking_photos_latent_resume_offset,
            kh3ap::runtime_twilight_town_help_latent_resume_opcode,
            kh3ap::runtime_twilight_town_taking_photos_bypass_continuation_offset,
            0x19,
            kh3ap::runtime_twilight_town_taking_photos_message_id_offset,
            kh3ap::runtime_twilight_town_taking_photos_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_twilight_town_taking_photos_help_bytecode,
    };

    constexpr TestProfile twilight_town_lucky_emblems_profile{
            kh3ap::runtime_twilight_town_tutorial_script_size,
            kh3ap::runtime_twilight_town_lucky_emblems_callsite_offset,
            0x20,
            kh3ap::runtime_twilight_town_lucky_emblems_callsite_offset,
            kh3ap::runtime_twilight_town_lucky_emblems_final_function_offset,
            kh3ap::runtime_twilight_town_lucky_emblems_latent_linkage_offset,
            kh3ap::runtime_twilight_town_lucky_emblems_latent_resume_offset,
            kh3ap::runtime_twilight_town_lucky_emblems_latent_resume_offset,
            kh3ap::runtime_twilight_town_help_latent_resume_opcode,
            kh3ap::runtime_twilight_town_lucky_emblems_bypass_continuation_offset,
            0x19,
            kh3ap::runtime_twilight_town_lucky_emblems_message_id_offset,
            kh3ap::runtime_twilight_town_lucky_emblems_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_twilight_town_lucky_emblems_help_bytecode,
    };

    constexpr TestProfile twilight_town_links_profile{
            kh3ap::runtime_twilight_town_tutorial_script_size,
            kh3ap::runtime_twilight_town_links_callsite_offset,
            0x20,
            kh3ap::runtime_twilight_town_links_callsite_offset,
            kh3ap::runtime_twilight_town_links_final_function_offset,
            kh3ap::runtime_twilight_town_links_latent_linkage_offset,
            kh3ap::runtime_twilight_town_links_latent_resume_offset,
            kh3ap::runtime_twilight_town_links_latent_resume_offset,
            kh3ap::runtime_twilight_town_help_latent_resume_opcode,
            kh3ap::runtime_twilight_town_links_latent_resume_offset,
            kh3ap::runtime_twilight_town_help_latent_resume_opcode,
            kh3ap::runtime_twilight_town_links_message_id_offset,
            kh3ap::runtime_twilight_town_links_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_twilight_town_links_help_bytecode,
            true,
            kh3ap::runtime_twilight_town_links_latent_resume_jump_target,
    };

    constexpr TestProfile twilight_town_meow_wow_profile{
            kh3ap::runtime_twilight_town_tutorial_script_size,
            kh3ap::runtime_twilight_town_meow_wow_callsite_offset,
            0x20,
            kh3ap::runtime_twilight_town_meow_wow_callsite_offset,
            kh3ap::runtime_twilight_town_meow_wow_final_function_offset,
            kh3ap::runtime_twilight_town_meow_wow_latent_linkage_offset,
            kh3ap::runtime_twilight_town_meow_wow_latent_resume_offset,
            kh3ap::runtime_twilight_town_meow_wow_latent_resume_offset,
            kh3ap::runtime_twilight_town_help_latent_resume_opcode,
            kh3ap::runtime_twilight_town_meow_wow_latent_resume_offset,
            kh3ap::runtime_twilight_town_help_latent_resume_opcode,
            kh3ap::runtime_twilight_town_meow_wow_message_id_offset,
            kh3ap::runtime_twilight_town_meow_wow_message_id,
            0,
            {},
            &kh3ap::try_patch_runtime_twilight_town_meow_wow_help_bytecode,
            true,
            kh3ap::runtime_twilight_town_meow_wow_latent_resume_jump_target,
    };

    constexpr TestAbilitySetProfile he01_attraction_flow_enable_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            kh3ap::runtime_olympus_he01_attraction_flow_enable_callsite_offset,
            kh3ap::runtime_olympus_he01_attraction_flow_enable_virtual_function_offset,
            kh3ap::runtime_olympus_he01_attraction_flow_enable_value_offset,
            0x27,
            kh3ap::runtime_olympus_he01_attraction_flow_enable_continuation_offset,
            kh3ap::runtime_olympus_he01_attraction_flow_enable_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he01_attraction_flow_enable_bytecode,
    };

    constexpr TestAbilitySetProfile he02_attraction_flow_enable_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            kh3ap::runtime_olympus_he02_attraction_flow_enable_callsite_offset,
            kh3ap::runtime_olympus_he02_attraction_flow_enable_virtual_function_offset,
            kh3ap::runtime_olympus_he02_attraction_flow_enable_value_offset,
            0x27,
            kh3ap::runtime_olympus_he02_attraction_flow_enable_continuation_offset,
            kh3ap::runtime_olympus_he02_attraction_flow_enable_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he02_attraction_flow_enable_bytecode,
    };

    constexpr TestAbilitySetProfile he03_attraction_flow_tutorial_enable_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_he03_attraction_flow_tutorial_enable_callsite_offset,
            kh3ap::runtime_olympus_he03_attraction_flow_tutorial_enable_virtual_function_offset,
            kh3ap::runtime_olympus_he03_attraction_flow_tutorial_enable_value_offset,
            0x27,
            kh3ap::runtime_olympus_attractions_tutorial_declined_continuation_offset,
            kh3ap::runtime_olympus_attractions_tutorial_declined_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he03_attraction_flow_tutorial_enable_bytecode,
    };

    constexpr TestAbilitySetProfile he03_attraction_flow_restore_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_he03_attraction_flow_restore_callsite_offset,
            kh3ap::runtime_olympus_he03_attraction_flow_restore_virtual_function_offset,
            kh3ap::runtime_olympus_he03_attraction_flow_restore_value_offset,
            0x27,
            kh3ap::runtime_olympus_he03_attraction_flow_restore_continuation_offset,
            kh3ap::runtime_olympus_he03_attraction_flow_restore_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he03_attraction_flow_restore_bytecode,
    };

    constexpr TestAbilitySetProfile he01_style_change_enable_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            kh3ap::runtime_olympus_he01_style_change_enable_callsite_offset,
            kh3ap::runtime_olympus_he01_style_change_enable_virtual_function_offset,
            kh3ap::runtime_olympus_he01_style_change_enable_value_offset,
            0x27,
            kh3ap::runtime_olympus_he01_style_change_enable_continuation_offset,
            kh3ap::runtime_olympus_he01_style_change_enable_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he01_style_change_enable_bytecode,
    };

    constexpr TestAbilitySetProfile he02_style_change_enable_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            kh3ap::runtime_olympus_he02_style_change_enable_callsite_offset,
            kh3ap::runtime_olympus_he02_style_change_enable_virtual_function_offset,
            kh3ap::runtime_olympus_he02_style_change_enable_value_offset,
            0x27,
            kh3ap::runtime_olympus_he02_style_change_enable_continuation_offset,
            kh3ap::runtime_olympus_he02_style_change_enable_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he02_style_change_enable_bytecode,
    };

    constexpr TestAbilitySetProfile he03_style_change_tutorial_enable_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_he03_style_change_tutorial_enable_callsite_offset,
            kh3ap::runtime_olympus_he03_style_change_tutorial_enable_virtual_function_offset,
            kh3ap::runtime_olympus_he03_style_change_tutorial_enable_value_offset,
            0x27,
            kh3ap::runtime_olympus_he03_style_change_tutorial_enable_continuation_offset,
            kh3ap::runtime_olympus_he03_style_change_tutorial_enable_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he03_style_change_tutorial_enable_bytecode,
    };

    constexpr TestAbilitySetProfile he03_style_change_restore_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            kh3ap::runtime_olympus_he03_style_change_restore_callsite_offset,
            kh3ap::runtime_olympus_he03_style_change_restore_virtual_function_offset,
            kh3ap::runtime_olympus_he03_style_change_restore_value_offset,
            0x27,
            kh3ap::runtime_olympus_he03_style_change_restore_continuation_offset,
            kh3ap::runtime_olympus_he03_style_change_restore_continuation_opcode,
            &kh3ap::try_patch_runtime_olympus_he03_style_change_restore_bytecode,
    };

    constexpr TestAbilitySetProfile he01_athletic_flow_enable_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            0x26372,
            0x26388,
            0x26395,
            0x27,
            0x26397,
            0x06,
    };

    constexpr TestAbilitySetProfile he01_shoot_flow_enable_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            0x263C7,
            0x263DD,
            0x263EA,
            0x27,
            0x263EC,
            0x06,
    };

    constexpr TestAbilitySetProfile he01_magic_enable_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            0x264C6,
            0x264DC,
            0x264E9,
            0x27,
            0x264EB,
            0x06,
    };

    constexpr TestAbilitySetProfile he01_wall_run_enable_profile{
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            0x2651B,
            0x26531,
            0x2653E,
            0x27,
            0x26540,
            0x06,
    };

    constexpr std::array<TestAbilitySetProfile, 6> he01_ability_set_profiles{
            he01_athletic_flow_enable_profile,
            he01_shoot_flow_enable_profile,
            he01_attraction_flow_enable_profile,
            he01_style_change_enable_profile,
            he01_magic_enable_profile,
            he01_wall_run_enable_profile,
    };

    constexpr TestAbilitySetProfile he02_wall_run_tutorial_disable_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x16284,
            0x1629A,
            0x162A7,
            0x28,
            0x162A9,
            0x5F,
    };

    constexpr TestAbilitySetProfile he02_magic_tutorial_enable_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x162D4,
            0x162EA,
            0x162F7,
            0x27,
            0x162F9,
            0x5F,
    };

    constexpr TestAbilitySetProfile he02_style_change_tutorial_disable_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x16324,
            0x1633A,
            0x16347,
            0x28,
            0x16349,
            0x5F,
    };

    constexpr TestAbilitySetProfile he02_attraction_flow_tutorial_disable_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x16374,
            0x1638A,
            0x16397,
            0x28,
            0x16399,
            0x5F,
    };

    constexpr TestAbilitySetProfile he02_shoot_flow_tutorial_disable_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x163C4,
            0x163DA,
            0x163E7,
            0x28,
            0x163E9,
            0x5F,
    };

    constexpr TestAbilitySetProfile he02_athletic_flow_restore_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x6F4ED,
            0x6F503,
            0x6F510,
            0x27,
            0x6F512,
            0x06,
    };

    constexpr TestAbilitySetProfile he02_shoot_flow_restore_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x6F542,
            0x6F558,
            0x6F565,
            0x27,
            0x6F567,
            0x06,
    };

    constexpr TestAbilitySetProfile he02_magic_restore_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x6F641,
            0x6F657,
            0x6F664,
            0x27,
            0x6F666,
            0x06,
    };

    constexpr TestAbilitySetProfile he02_wall_run_restore_profile{
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0x6F696,
            0x6F6AC,
            0x6F6B9,
            0x27,
            0x6F6BB,
            0x06,
    };

    constexpr std::array<TestAbilitySetProfile, 11> he02_ability_set_profiles{
            he02_wall_run_tutorial_disable_profile,
            he02_magic_tutorial_enable_profile,
            he02_style_change_tutorial_disable_profile,
            he02_attraction_flow_tutorial_disable_profile,
            he02_shoot_flow_tutorial_disable_profile,
            he02_athletic_flow_restore_profile,
            he02_shoot_flow_restore_profile,
            he02_attraction_flow_enable_profile,
            he02_style_change_enable_profile,
            he02_magic_restore_profile,
            he02_wall_run_restore_profile,
    };

    constexpr TestAbilitySetProfile he03_wall_run_restore_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            0x27EF7,
            0x27F0D,
            0x27F1A,
            0x27,
            0x27F1C,
            0x5F,
    };

    constexpr TestAbilitySetProfile he03_magic_restore_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            0x27F47,
            0x27F5D,
            0x27F6A,
            0x27,
            0x27F6C,
            0x5F,
    };

    constexpr TestAbilitySetProfile he03_shoot_flow_disable_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            0x28037,
            0x2804D,
            0x2805A,
            0x28,
            0x2805C,
            0x5F,
    };

    constexpr TestAbilitySetProfile he03_athletic_flow_disable_profile{
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            0x28087,
            0x2809D,
            0x280AA,
            0x28,
            0x280AC,
            0x14,
    };

    constexpr std::array<TestAbilitySetProfile, 8> he03_ability_set_profiles{
            he03_style_change_tutorial_enable_profile,
            he03_attraction_flow_tutorial_enable_profile,
            he03_wall_run_restore_profile,
            he03_magic_restore_profile,
            he03_style_change_restore_profile,
            he03_attraction_flow_restore_profile,
            he03_shoot_flow_disable_profile,
            he03_athletic_flow_disable_profile,
    };

    constexpr auto encoded_offset_instruction(
            uint8_t opcode,
            size_t target) -> std::array<uint8_t, 5>
    {
        return {
                opcode,
                static_cast<uint8_t>(target & 0xFF),
                static_cast<uint8_t>((target >> 8) & 0xFF),
                static_cast<uint8_t>((target >> 16) & 0xFF),
                static_cast<uint8_t>((target >> 24) & 0xFF),
        };
    }

    template <size_t Size>
    auto write_pattern(
            std::vector<uint8_t>& script,
            size_t offset,
            const std::array<uint8_t, Size>& pattern) -> void
    {
        std::copy(pattern.begin(), pattern.end(), script.begin() + offset);
    }

    auto write_valid_profile(
            std::vector<uint8_t>& script,
            const TestProfile& profile) -> void
    {
        assert(script.size() == profile.script_size);
        script[profile.bypass_entry_offset] = 0x19;
        script[profile.bypass_entry_offset + 1] =
                profile.bypass_entry_object_opcode;
        script[profile.tutorial_callsite_offset] = 0x19;
        script[profile.tutorial_callsite_offset + 1] = 0x20;
        script[profile.final_function_offset] = 0x1C;
        write_pattern(
                script,
                profile.latent_linkage_offset,
                encoded_offset_instruction(0x5B, profile.latent_resume_offset));
        if (profile.latent_resume_has_no_bypass_branch)
        {
            assert(profile.latent_resume_offset ==
                   profile.tutorial_continuation_offset);
            if (profile.latent_resume_direct_jump_target != 0)
            {
                write_pattern(
                        script,
                        profile.latent_resume_offset,
                        encoded_offset_instruction(
                                0x07,
                                profile.latent_resume_direct_jump_target));
                script[profile.latent_resume_offset + 5] = 0x27;
            }
            else
            {
                script[profile.latent_resume_offset] =
                        profile.tutorial_continuation_opcode;
            }
        }
        else
        {
            write_pattern(
                    script,
                    profile.latent_resume_offset,
                    encoded_offset_instruction(
                            0x07,
                            profile.bypass_continuation_offset));
        }
        script[profile.tutorial_continuation_offset] =
                profile.tutorial_continuation_opcode;
        script[profile.bypass_continuation_offset] =
                profile.bypass_continuation_opcode;
        if (profile.patched_bypass_continuation_offset != 0)
        {
            script[profile.patched_bypass_continuation_offset] =
                    profile.patched_bypass_continuation_opcode;
        }
        if (profile.bypass_followup_help.callsite_offset != 0)
        {
            script[profile.bypass_followup_help.callsite_offset] = 0x19;
            script[profile.bypass_followup_help.callsite_offset + 1] = 0x20;
            script[profile.bypass_followup_help.final_function_offset] = 0x1C;
            write_pattern(
                    script,
                    profile.bypass_followup_help.latent_linkage_offset,
                    encoded_offset_instruction(
                            0x5B,
                            profile.bypass_followup_help.latent_resume_offset));
            write_pattern(
                    script,
                    profile.bypass_followup_help.latent_resume_offset,
                    encoded_offset_instruction(
                            0x07,
                            profile.bypass_followup_help.latent_resume_jump_target));
            script[profile.bypass_followup_help.latent_resume_offset + 5] = 0x27;
            script[profile.bypass_followup_help.message_id_offset] = 0x24;
            script[profile.bypass_followup_help.message_id_offset + 1] =
                    profile.bypass_followup_help.message_id;
        }
        script[profile.message_id_offset] = 0x24;
        script[profile.message_id_offset + 1] = profile.message_id;
        if (!profile.message_key.empty())
        {
            script[profile.message_key_offset] = 0x1F;
            std::copy(
                    profile.message_key.begin(),
                    profile.message_key.end(),
                    script.begin() + profile.message_key_offset + 1);
            script[profile.message_key_offset + 1 + profile.message_key.size()] = 0;
        }
    }

    auto make_valid_script(const TestProfile& profile) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> script(profile.script_size, 0xCC);
        write_valid_profile(script, profile);
        return script;
    }

    auto write_valid_ability_set_profile(
            std::vector<uint8_t>& script,
            const TestAbilitySetProfile& profile) -> void
    {
        assert(script.size() == profile.script_size);
        script[profile.callsite_offset] = 0x19;
        script[profile.callsite_offset + 1] = 0x00;
        script[profile.virtual_function_offset] = 0x1B;
        script[profile.value_offset] = profile.value_opcode;
        script[profile.value_offset + 1] = 0x16;
        script[profile.continuation_offset] = profile.continuation_opcode;
    }

    auto make_valid_ability_set_script(
            const TestAbilitySetProfile& profile) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> script(profile.script_size, 0xCC);
        write_valid_ability_set_profile(script, profile);
        return script;
    }

    auto invoke_ability_set_patch(
            const TestAbilitySetProfile& profile,
            uint8_t* script,
            size_t script_size) -> kh3ap::RuntimeTutorialPopupBypassResult
    {
        if (profile.patch)
        {
            return profile.patch(script, script_size);
        }
        return kh3ap::try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                kh3ap::RuntimeAbilitySetBypassProfile{
                        profile.script_size,
                        profile.callsite_offset,
                        profile.virtual_function_offset,
                        profile.value_offset,
                        profile.value_opcode,
                        profile.continuation_offset,
                        profile.continuation_opcode,
                });
    }

    auto run_profile_tests(const TestProfile& profile) -> void
    {
        {
            auto script = make_valid_script(profile);
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Patched);

            const size_t patched_bypass_continuation_offset =
                    profile.patched_bypass_continuation_offset != 0
                    ? profile.patched_bypass_continuation_offset
                    : profile.bypass_continuation_offset;
            const auto expected_jump = encoded_offset_instruction(
                    0x06,
                    patched_bypass_continuation_offset);
            const auto wrong_tutorial_jump = encoded_offset_instruction(
                    0x06,
                    profile.tutorial_continuation_offset);
            assert(std::equal(
                    expected_jump.begin(),
                    expected_jump.end(),
                    script.begin() + profile.bypass_entry_offset));
            if (profile.tutorial_continuation_offset !=
                patched_bypass_continuation_offset)
            {
                assert(!std::equal(
                        wrong_tutorial_jump.begin(),
                        wrong_tutorial_jump.end(),
                        script.begin() + profile.bypass_entry_offset));
            }
            for (size_t index = 0; index < script.size(); ++index)
            {
                const bool is_callsite_byte =
                        index >= profile.bypass_entry_offset &&
                        index < profile.bypass_entry_offset + expected_jump.size();
                assert(is_callsite_byte || script[index] == before[index]);
            }

            const auto after_patch = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::AlreadyPatched);
            assert(script == after_patch);
        }

        {
            auto script = make_valid_script(profile);
            script[profile.bypass_entry_offset + 1] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(profile);
            script[profile.tutorial_callsite_offset + 1] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(profile);
            script[profile.message_id_offset + 1] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        if (!profile.message_key.empty())
        {
            auto script = make_valid_script(profile);
            script[profile.message_key_offset + 1] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(profile);
            script[profile.tutorial_continuation_offset] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(profile);
            script[profile.bypass_continuation_offset] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_script(profile);
            const size_t wrong_latent_resume_target =
                    profile.tutorial_continuation_offset !=
                            profile.bypass_continuation_offset
                    ? profile.tutorial_continuation_offset
                    : profile.bypass_continuation_offset + 1;
            write_pattern(
                    script,
                    profile.latent_resume_offset,
                    encoded_offset_instruction(0x07, wrong_latent_resume_target));
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        if (profile.patched_bypass_continuation_offset != 0)
        {
            auto script = make_valid_script(profile);
            script[profile.patched_bypass_continuation_offset] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        if (profile.latent_resume_direct_jump_target != 0)
        {
            auto script = make_valid_script(profile);
            script[profile.latent_resume_offset + 5] ^= 0x01;
            const auto before = script;
            assert(profile.patch(script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        if (profile.bypass_followup_help.callsite_offset != 0)
        {
            for (const size_t corrupt_offset : {
                         profile.bypass_followup_help.callsite_offset + 1,
                         profile.bypass_followup_help.final_function_offset,
                         profile.bypass_followup_help.latent_linkage_offset,
                         profile.bypass_followup_help.latent_resume_offset,
                         profile.bypass_followup_help.latent_resume_offset + 5,
                         profile.bypass_followup_help.message_id_offset + 1})
            {
                auto script = make_valid_script(profile);
                script[corrupt_offset] ^= 0x01;
                const auto before = script;
                assert(profile.patch(script.data(), script.size()) ==
                       kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
                assert(script == before);
            }
        }

        std::vector<uint8_t> wrong_size(1024, 0xCC);
        assert(profile.patch(wrong_size.data(), wrong_size.size()) ==
               kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
        assert(profile.patch(nullptr, profile.script_size) ==
               kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
    }

    auto run_ability_set_profile_tests(
            const TestAbilitySetProfile& profile) -> void
    {
        {
            auto script = make_valid_ability_set_script(profile);
            const auto before = script;
            assert(invoke_ability_set_patch(profile, script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Patched);

            const auto expected_jump = encoded_offset_instruction(
                    0x06,
                    profile.continuation_offset);
            assert(std::equal(
                    expected_jump.begin(),
                    expected_jump.end(),
                    script.begin() + profile.callsite_offset));
            for (size_t index = 0; index < script.size(); ++index)
            {
                const bool is_callsite_byte =
                        index >= profile.callsite_offset &&
                        index < profile.callsite_offset + expected_jump.size();
                assert(is_callsite_byte || script[index] == before[index]);
            }

            const auto after_patch = script;
            assert(invoke_ability_set_patch(profile, script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::AlreadyPatched);
            assert(script == after_patch);
        }

        for (const size_t corrupt_offset : {
                     profile.callsite_offset + 1,
                     profile.virtual_function_offset,
                     profile.value_offset,
                     profile.value_offset + 1,
                     profile.continuation_offset})
        {
            auto script = make_valid_ability_set_script(profile);
            script[corrupt_offset] ^= 0x01;
            const auto before = script;
            assert(invoke_ability_set_patch(profile, script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        {
            auto script = make_valid_ability_set_script(profile);
            script[profile.value_offset] =
                    profile.value_opcode == 0x27 ? 0x28 : 0x27;
            const auto before = script;
            assert(invoke_ability_set_patch(profile, script.data(), script.size()) ==
                   kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
            assert(script == before);
        }

        std::vector<uint8_t> wrong_size(1024, 0xCC);
        assert(invoke_ability_set_patch(
                       profile,
                       wrong_size.data(),
                       wrong_size.size()) ==
               kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
        assert(invoke_ability_set_patch(profile, nullptr, profile.script_size) ==
               kh3ap::RuntimeTutorialPopupBypassResult::Rejected);
    }

    template <size_t ProfileCount>
    auto write_valid_ability_set_profiles(
            std::vector<uint8_t>& script,
            const std::array<TestAbilitySetProfile, ProfileCount>& profiles) -> void
    {
        for (const TestAbilitySetProfile& profile : profiles)
        {
            write_valid_ability_set_profile(script, profile);
        }
    }

    template <size_t ProfileCount>
    auto assert_ability_set_profiles_patched(
            const std::vector<uint8_t>& script,
            const std::array<TestAbilitySetProfile, ProfileCount>& profiles) -> void
    {
        for (const TestAbilitySetProfile& profile : profiles)
        {
            const auto expected_jump = encoded_offset_instruction(
                    0x06,
                    profile.continuation_offset);
            assert(std::equal(
                    expected_jump.begin(),
                    expected_jump.end(),
                    script.begin() + profile.callsite_offset));
        }
    }
}

void test_informational_help()
{
    using namespace kh3ap;
    using Result = RuntimeTutorialPopupBypassResult;
    for (const auto& profiles : {runtime_informational_help_profiles, runtime_informational_help_steam_profiles})
    for (size_t variant = 0; variant < 2; ++variant)
    {
        const size_t first = variant == 0 ? 0 : 3;
        const size_t end = variant == 0 ? 3 : 4;
        std::vector<uint8_t> original(profiles[first].script_size, 0x53);
        for (size_t i = first; i < end; ++i)
        {
            const auto& p = profiles[i];
            original[p.bypass_entry_offset] = 0x19;
            original[p.bypass_entry_offset + 1] = 0x20;
            original[p.final_function_offset] = 0x1C;
            write_pattern(original, p.latent_linkage_offset, encoded_offset_instruction(0x5B, p.latent_resume_offset));
            write_pattern(original, p.latent_resume_offset, encoded_offset_instruction(0x07, p.latent_resume_direct_jump_target));
            original[p.latent_resume_offset + 5] = 0x27;
            original[p.message_id_offset] = 0x24;
            original[p.message_id_offset + 1] = p.message_id;
        }
        auto script = original;
        auto expected = original;
        for (size_t i = first; i < end; ++i)
        {
            const auto& p = profiles[i];
            write_pattern(expected, p.bypass_entry_offset, encoded_offset_instruction(0x06, p.latent_resume_offset));
        }
        assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size(), variant) == Result::Patched);
        assert(script == expected); // Only five bytes per help call may change.
        assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size(), variant) == Result::AlreadyPatched);
        for (size_t i = first; i < end; ++i)
        {
            const auto& p = profiles[i];
            for (const size_t offset : {p.message_id_offset + 1, p.latent_linkage_offset + 1,
                    p.final_function_offset, p.latent_resume_offset + 1, p.latent_resume_offset + 5})
            {
                script = original;
                script[offset] ^= 1;
                const auto before = script;
                assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size(), variant) == Result::Rejected);
                assert(script == before); // Including rollback after a later bad panel.
            }
        }
        script = original;
        assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size() - 1, variant) == Result::Rejected);
        assert(script == original);
    }
    assert(try_patch_runtime_informational_help_bytecode(nullptr, 0, 0) == Result::Rejected);
    assert(try_patch_runtime_informational_help_bytecode(nullptr, 0, 2) == Result::Rejected);
}

void test_olympus_opening()
{
    using namespace kh3ap;
    using Result = RuntimeTutorialPopupBypassResult;
    for (bool camera : {false, true})
    {
        std::vector<uint8_t> script(camera ? 0x1026 : 0x7116C, 0xCC);
        const auto put = [&](size_t offset, std::initializer_list<uint8_t> bytes) {
            std::copy(bytes.begin(), bytes.end(), script.begin() + offset);
        };
        if (camera)
        {
            put(0xEF0, {0x19, 0x20}); put(0xF06, {0x1C});
            put(0xF1E, {0x19}); put(0xF34, {0x1B});
            put(0xF66, {6, 0xC4, 5, 0, 0});
            put(0xFBA, {6, 0x0F, 0, 0, 0});
            put(0x94D, {0x5B, 0x80, 5, 0, 0});
            put(0x580, {0x14}); put(0x5AB, {0x1C}); put(0x5E4, {0x1C});
            put(0x1F3, {0x1B}); put(0xF6B, {0x19, 0x20});
            put(0xF81, {0x1C}); put(0xF8E, {0x19}); put(0xFA4, {0x1C});
        }
        else
        {
            put(0x1F7DB, {0x0F}); put(0x1F754, {0x1F});
            constexpr char key[] = "system_0010";
            std::copy(std::begin(key), std::end(key), script.begin() + 0x1F755);
            put(0x1F803, {0x1C}); put(0x1F81A, {0x5B, 0x4B, 0xF8, 1, 0});
            put(0x1F84B, {0x19, 0x20}); put(0x1F861, {0x1C});
            put(0x1F881, {0x5B, 0x9C, 0xF8, 1, 0});
            put(0x1F89C, {0x19, 0x20}); put(0x1F8B2, {0x1C}); put(0x1F8BF, {0x1B});
            put(0x6AFEB, {0x07, 0xA2, 0xB1, 0x06, 0}); put(0x6AFF0, {0x27});
            put(0x6AFB1, {0x19, 0x20}); put(0x6AFC7, {0x1C});
            put(0x9440, {0x14, 0x00}); put(0x944A, {0x19, 0x20});
            put(0x9460, {0x1C}); put(0x946A, {0x21}); put(0x9478, {0x4D});
        }
        const auto before = script;
        const auto variant = camera ? 3 : 2;
        const std::vector<std::pair<size_t, size_t>> jumps = camera
                ? std::vector<std::pair<size_t, size_t>>{
                        {0xEF0, 0xF1E}, {0xF66, 0x580},
                        {0xF6B, 0xF8E}, {0xFBA, 0x1F3}}
                : std::vector<std::pair<size_t, size_t>>{
                        {0x1F7DB, 0x1F89C}, {0x6AFEB, 0x9440}};
        auto expected = before;
        for (const auto& [offset, target] : jumps)
            write_pattern(expected, offset, encoded_offset_instruction(0x06, target));
        assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size(), variant) == Result::Patched);
        assert(script == expected); // Exact destinations; trigger/story/visibility code stays intact.
        const auto after = script;
        assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size(), variant) == Result::AlreadyPatched);
        assert(script == after);
        // The early observer and later reconciliation may see any subset of the
        // jumps already installed. Complete it atomically and keep all other bytes.
        for (unsigned mask = 0; mask < (1u << jumps.size()) - 1; ++mask)
        {
            script = before;
            for (size_t i = 0; i < jumps.size(); ++i)
                if (mask & (1u << i))
                    write_pattern(script, jumps[i].first,
                            encoded_offset_instruction(0x06, jumps[i].second));
            assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size(), variant) == Result::Patched);
            assert(script == expected);
        }
        // Corrupt each guarded byte independently. No partial camera mutation,
        // including when the second jump/cleanup is the rejected anchor.
        for (size_t i = 0; i < before.size(); ++i)
        {
            if (before[i] == 0xCC) continue;
            auto bad = before;
            bad[i] ^= 0x80;
            const auto unchanged = bad;
            assert(try_patch_runtime_informational_help_bytecode(bad.data(), bad.size(), variant) == Result::Rejected);
            assert(bad == unchanged);
        }
        script = before;
        assert(try_patch_runtime_informational_help_bytecode(script.data(), script.size() - 1, variant) == Result::Rejected);
        assert(script == before);
    }
}

int main()
{
    test_olympus_opening();
    test_informational_help();
    for (const bool is_ew04 : {false, true})
    {
        using Result = kh3ap::RuntimeTutorialPopupBypassResult;
        constexpr std::array<size_t, 15> ew01_offsets{
                15013, 15093, 15173, 28593, 28673, 28753,
                58174, 58254, 58334, 81766, 81846, 81926,
                80566, 81366, 81046};
        constexpr std::array<size_t, 15> ew04_offsets{
                16278, 16358, 16438, 29395, 29475, 29555,
                58292, 58372, 58452, 84629, 84709, 84789,
                83429, 84229, 83909};
        const auto& offsets = is_ew04 ? ew04_offsets : ew01_offsets;
        auto script = make_valid_script(is_ew04 ? ew04_profile : ew01_profile);
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            const size_t offset = offsets[i];
            write_valid_ability_set_profile(script, {
                    script.size(), offset, offset + 22, offset + 35,
                    static_cast<uint8_t>(i < 12 ? 0x28 : 0x27), offset + 37, 0x5F});
        }
        const auto original = script;
        auto patch = [](auto& bytes) {
            return kh3ap::try_patch_runtime_final_world_ability_disables_bytecode(
                    bytes.data(), bytes.size());
        };
        assert(patch(script) == Result::Patched);
        for (size_t i = 0; i < script.size(); ++i)
        {
            const bool is_jump = std::any_of(offsets.begin(), offsets.end(),
                    [i](size_t offset) { return i >= offset && i < offset + 5; });
            if (!is_jump) assert(script[i] == original[i]);
        }
        for (const size_t offset : offsets)
        {
            assert(script[offset] == 0x06);
            for (size_t byte = 0; byte < 4; ++byte)
                assert(script[offset + 1 + byte] == ((offset + 37) >> (8 * byte) & 0xFF));
        }
        const auto patched = script;
        assert(patch(script) == Result::AlreadyPatched && script == patched);
        assert(kh3ap::try_patch_runtime_final_world_combat_basics_tutorial_bytecode(
                script.data(), script.size()) == Result::Patched);
        assert(patch(script) == Result::AlreadyPatched); // Independent tutorial jumps coexist.
        for (const size_t corrupt : {offsets.back() + 35, offsets.back() + 37})
        {
            auto rejected = original;
            rejected[corrupt] = 0xCC;
            const auto before = rejected;
            assert(patch(rejected) == Result::Rejected && rejected == before);
        }
        script.pop_back();
        assert(patch(script) == Result::Rejected);
        assert(kh3ap::try_patch_runtime_final_world_ability_disables_bytecode(
                nullptr, original.size()) == Result::Rejected);
    }
    run_profile_tests(ew01_profile);
    run_profile_tests(ew04_profile);
    run_profile_tests(he01_team_attacks_help_profile);
    run_profile_tests(shotlock_profile);
    run_profile_tests(airstepping_profile);
    run_profile_tests(casting_magic_profile);
    run_profile_tests(free_running_profile);
    run_profile_tests(flowmotion_help_profile);
    run_profile_tests(attractions_help_profile);
    run_profile_tests(flowmotion_tutorial_profile);
    run_profile_tests(team_attacks_help_profile);
    run_profile_tests(grand_magic_help_profile);
    run_profile_tests(formchange_tutorial_profile);
    run_profile_tests(attractions_tutorial_profile);
    run_profile_tests(save_points_profile);
    run_profile_tests(moogle_shop_profile);
    run_profile_tests(toy_box_aerial_recovery_profile);
    run_profile_tests(toy_box_gigas_profile);
    run_profile_tests(toy_box_mag_assemble_profile);
    run_profile_tests(toy_box_eight_bit_blast_profile);
    run_profile_tests(caribbean_sea_spectacle_profile);

    // The help window's latent completion owns TresEndCinematicMode. The patch
    // must retain the delay and balanced cinematic wrapper, route directly to
    // that cleanup, and leave the separate CA_ISLAND_EVENT3 branch untouched.
    auto caribbean_script = make_valid_script(caribbean_sea_spectacle_profile);
    constexpr std::array<std::pair<size_t, uint8_t>, 7>
            caribbean_preserved_anchors{{
                    {kh3ap::runtime_caribbean_sea_spectacle_delay_callsite_offset, 0x19},
                    {kh3ap::runtime_caribbean_sea_spectacle_delay_final_function_offset, 0x1C},
                    {kh3ap::runtime_caribbean_sea_spectacle_start_cinematic_callsite_offset, 0x19},
                    {kh3ap::runtime_caribbean_sea_spectacle_start_cinematic_final_function_offset, 0x1C},
                    {kh3ap::runtime_caribbean_sea_spectacle_post_dismiss_offset, 0x19},
                    {kh3ap::runtime_caribbean_sea_spectacle_end_cinematic_final_function_offset, 0x1C},
                    {kh3ap::runtime_caribbean_sea_spectacle_island_event3_entry_offset, 0x14},
            }};
    for (const auto& [offset, opcode] : caribbean_preserved_anchors)
    {
        caribbean_script[offset] = opcode;
    }
    const auto caribbean_before = caribbean_script;
    assert(kh3ap::try_patch_runtime_caribbean_sea_spectacle_help_bytecode(
                   caribbean_script.data(),
                   caribbean_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    for (const auto& [offset, opcode] : caribbean_preserved_anchors)
    {
        assert(caribbean_script[offset] == opcode);
        assert(caribbean_script[offset] == caribbean_before[offset]);
    }
    const auto caribbean_cleanup_jump = encoded_offset_instruction(
            0x06,
            kh3ap::runtime_caribbean_sea_spectacle_post_dismiss_offset);
    assert(std::equal(
            caribbean_cleanup_jump.begin(),
            caribbean_cleanup_jump.end(),
            caribbean_script.begin() +
                    kh3ap::runtime_caribbean_sea_spectacle_bypass_entry_offset));
    run_profile_tests(twilight_town_taking_photos_profile);
    run_profile_tests(twilight_town_lucky_emblems_profile);
    run_profile_tests(twilight_town_links_profile);
    run_profile_tests(twilight_town_meow_wow_profile);

    // All four Twilight Town patches share tt_01_gameflow. Verify their
    // combined mutation footprint stays confined to the four Game Help call
    // entries; in particular, the persistent camera-help flag write remains
    // untouched after the Taking Photos call is bypassed.
    std::vector<uint8_t> twilight_town_script(
            kh3ap::runtime_twilight_town_tutorial_script_size,
            0xCC);
    write_valid_profile(
            twilight_town_script,
            twilight_town_taking_photos_profile);
    write_valid_profile(
            twilight_town_script,
            twilight_town_lucky_emblems_profile);
    write_valid_profile(
            twilight_town_script,
            twilight_town_links_profile);
    write_valid_profile(
            twilight_town_script,
            twilight_town_meow_wow_profile);
    constexpr size_t taking_photos_help_flag_write_offset = 0xEBFB;
    twilight_town_script[taking_photos_help_flag_write_offset] = 0x19;
    const auto twilight_town_before = twilight_town_script;
    assert(kh3ap::try_patch_runtime_twilight_town_taking_photos_help_bytecode(
                   twilight_town_script.data(),
                   twilight_town_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_twilight_town_lucky_emblems_help_bytecode(
                   twilight_town_script.data(),
                   twilight_town_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_twilight_town_links_help_bytecode(
                   twilight_town_script.data(),
                   twilight_town_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_twilight_town_meow_wow_help_bytecode(
                   twilight_town_script.data(),
                   twilight_town_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    for (size_t index = 0; index < twilight_town_script.size(); ++index)
    {
        const auto within_callsite = [index](size_t callsite_offset) {
            return index >= callsite_offset && index < callsite_offset + 5;
        };
        const bool is_patched_callsite =
                within_callsite(
                        kh3ap::runtime_twilight_town_taking_photos_callsite_offset) ||
                within_callsite(
                        kh3ap::runtime_twilight_town_lucky_emblems_callsite_offset) ||
                within_callsite(
                        kh3ap::runtime_twilight_town_links_callsite_offset) ||
                within_callsite(
                        kh3ap::runtime_twilight_town_meow_wow_callsite_offset);
        assert(is_patched_callsite ||
               twilight_town_script[index] == twilight_town_before[index]);
    }
    assert(twilight_town_script[taking_photos_help_flag_write_offset] ==
           twilight_town_before[taking_photos_help_flag_write_offset]);

    for (const TestAbilitySetProfile& profile : he01_ability_set_profiles)
    {
        run_ability_set_profile_tests(profile);
    }
    for (const TestAbilitySetProfile& profile : he02_ability_set_profiles)
    {
        run_ability_set_profile_tests(profile);
    }
    for (const TestAbilitySetProfile& profile : he03_ability_set_profiles)
    {
        run_ability_set_profile_tests(profile);
    }

    std::vector<uint8_t> he01_script(
            kh3ap::runtime_olympus_he01_gameflow_script_size,
            0xCC);
    write_valid_profile(he01_script, he01_team_attacks_help_profile);
    write_valid_profile(he01_script, shotlock_profile);
    write_valid_profile(he01_script, airstepping_profile);
    write_valid_ability_set_profiles(he01_script, he01_ability_set_profiles);
    assert(kh3ap::try_patch_runtime_olympus_he01_team_attacks_help_bytecode(
                   he01_script.data(),
                   he01_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_shotlock_tutorial_bytecode(
                   he01_script.data(),
                   he01_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_airstepping_tutorial_bytecode(
                   he01_script.data(),
                   he01_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_he01_tutorial_ability_sets_bytecode(
                   he01_script.data(),
                   he01_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert_ability_set_profiles_patched(he01_script, he01_ability_set_profiles);
    assert(kh3ap::try_patch_runtime_olympus_he01_tutorial_ability_sets_bytecode(
                   he01_script.data(),
                   he01_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::AlreadyPatched);

    std::vector<uint8_t> he02_script(
            kh3ap::runtime_olympus_early_tutorial_he02_script_size,
            0xCC);
    write_valid_profile(he02_script, casting_magic_profile);
    write_valid_profile(he02_script, free_running_profile);
    write_valid_profile(he02_script, flowmotion_help_profile);
    write_valid_profile(he02_script, attractions_help_profile);
    write_valid_ability_set_profiles(he02_script, he02_ability_set_profiles);
    assert(kh3ap::try_patch_runtime_olympus_casting_magic_tutorial_bytecode(
                   he02_script.data(),
                   he02_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_free_running_tutorial_bytecode(
                   he02_script.data(),
                   he02_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_flowmotion_help_bytecode(
                   he02_script.data(),
                   he02_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_attractions_help_bytecode(
                   he02_script.data(),
                   he02_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_he02_tutorial_ability_sets_bytecode(
                   he02_script.data(),
                   he02_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert_ability_set_profiles_patched(he02_script, he02_ability_set_profiles);
    assert(kh3ap::try_patch_runtime_olympus_he02_tutorial_ability_sets_bytecode(
                   he02_script.data(),
                   he02_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::AlreadyPatched);

    std::vector<uint8_t> he03_script(
            kh3ap::runtime_olympus_he03_tutorial_script_size,
            0xCC);
    write_valid_profile(he03_script, flowmotion_tutorial_profile);
    write_valid_profile(he03_script, team_attacks_help_profile);
    write_valid_profile(he03_script, grand_magic_help_profile);
    write_valid_profile(he03_script, formchange_tutorial_profile);
    write_valid_profile(he03_script, attractions_tutorial_profile);
    write_valid_profile(he03_script, save_points_profile);
    write_valid_profile(he03_script, moogle_shop_profile);
    write_valid_ability_set_profiles(he03_script, he03_ability_set_profiles);
    assert(kh3ap::try_patch_runtime_olympus_flowmotion_tutorial_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_team_attacks_help_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_grand_magic_help_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_formchange_tutorial_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_attractions_tutorial_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_he03_tutorial_ability_sets_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert_ability_set_profiles_patched(he03_script, he03_ability_set_profiles);
    assert(kh3ap::try_patch_runtime_olympus_he03_tutorial_ability_sets_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::AlreadyPatched);
    assert(kh3ap::try_patch_runtime_olympus_save_points_tutorial_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    assert(kh3ap::try_patch_runtime_olympus_moogle_shop_tutorial_bytecode(
                   he03_script.data(),
                   he03_script.size()) ==
           kh3ap::RuntimeTutorialPopupBypassResult::Patched);
    return 0;
}
