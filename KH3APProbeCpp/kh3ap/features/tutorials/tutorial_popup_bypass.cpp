#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr uint8_t ex_jump = 0x06;
        constexpr uint8_t ex_jump_if_not = 0x07;
        constexpr uint8_t ex_skip_offset_const = 0x5B;
        constexpr uint8_t ex_local_variable = 0x00;
        constexpr uint8_t ex_context = 0x19;
        constexpr uint8_t ex_end_function_parms = 0x16;
        constexpr uint8_t ex_virtual_function = 0x1B;
        constexpr uint8_t ex_final_function = 0x1C;
        constexpr uint8_t ex_string_const = 0x1F;
        constexpr uint8_t ex_object_const = 0x20;
        constexpr uint8_t ex_byte_const = 0x24;
        constexpr uint8_t ex_true = 0x27;
        constexpr uint8_t ex_false = 0x28;

        constexpr RuntimeTutorialPopupBypassProfile s_final_world_combat_basics_ew01{
                runtime_final_world_combat_basics_ew01_script_size,
                runtime_final_world_combat_basics_ew01_callsite_offset,
                ex_object_const,
                runtime_final_world_combat_basics_ew01_callsite_offset,
                runtime_final_world_combat_basics_ew01_final_function_offset,
                runtime_final_world_combat_basics_ew01_latent_linkage_offset,
                runtime_final_world_combat_basics_ew01_latent_resume_offset,
                runtime_final_world_combat_basics_ew01_tutorial_continuation_offset,
                runtime_final_world_combat_basics_continuation_opcode,
                runtime_final_world_combat_basics_ew01_bypass_continuation_offset,
                runtime_final_world_combat_basics_continuation_opcode,
                runtime_final_world_combat_basics_ew01_message_id_offset,
                runtime_final_world_combat_basics_message_id,
                runtime_final_world_combat_basics_ew01_message_key_offset,
                runtime_final_world_combat_basics_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_final_world_combat_basics_ew04{
                runtime_final_world_combat_basics_ew04_script_size,
                runtime_final_world_combat_basics_ew04_callsite_offset,
                ex_object_const,
                runtime_final_world_combat_basics_ew04_callsite_offset,
                runtime_final_world_combat_basics_ew04_final_function_offset,
                runtime_final_world_combat_basics_ew04_latent_linkage_offset,
                runtime_final_world_combat_basics_ew04_latent_resume_offset,
                runtime_final_world_combat_basics_ew04_tutorial_continuation_offset,
                runtime_final_world_combat_basics_continuation_opcode,
                runtime_final_world_combat_basics_ew04_bypass_continuation_offset,
                runtime_final_world_combat_basics_continuation_opcode,
                runtime_final_world_combat_basics_ew04_message_id_offset,
                runtime_final_world_combat_basics_message_id,
                runtime_final_world_combat_basics_ew04_message_key_offset,
                runtime_final_world_combat_basics_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_he01_team_attacks_help{
                runtime_olympus_he01_gameflow_script_size,
                runtime_olympus_he01_team_attacks_help_callsite_offset,
                ex_object_const,
                runtime_olympus_he01_team_attacks_help_callsite_offset,
                runtime_olympus_he01_team_attacks_help_final_function_offset,
                runtime_olympus_he01_team_attacks_help_latent_linkage_offset,
                runtime_olympus_he01_team_attacks_help_latent_resume_offset,
                runtime_olympus_he01_team_attacks_help_latent_resume_offset,
                runtime_olympus_he01_team_attacks_help_latent_resume_opcode,
                runtime_olympus_he01_team_attacks_help_continuation_offset,
                runtime_olympus_he01_team_attacks_help_continuation_opcode,
                runtime_olympus_he01_team_attacks_help_message_id_offset,
                runtime_olympus_he01_team_attacks_help_message_id,
                0,
                {},
                true,
                runtime_olympus_he01_team_attacks_help_latent_resume_jump_target,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_shotlock{
                runtime_olympus_he01_gameflow_script_size,
                runtime_olympus_shotlock_bypass_entry_offset,
                ex_local_variable,
                runtime_olympus_shotlock_tutorial_callsite_offset,
                runtime_olympus_shotlock_final_function_offset,
                runtime_olympus_shotlock_latent_linkage_offset,
                runtime_olympus_shotlock_latent_resume_offset,
                runtime_olympus_shotlock_tutorial_continuation_offset,
                runtime_olympus_shotlock_continuation_opcode,
                runtime_olympus_shotlock_bypass_continuation_offset,
                runtime_olympus_shotlock_continuation_opcode,
                runtime_olympus_shotlock_message_id_offset,
                runtime_olympus_shotlock_message_id,
                runtime_olympus_shotlock_message_key_offset,
                runtime_olympus_shotlock_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_airstepping{
                runtime_olympus_he01_gameflow_script_size,
                runtime_olympus_airstepping_bypass_entry_offset,
                ex_local_variable,
                runtime_olympus_airstepping_tutorial_callsite_offset,
                runtime_olympus_airstepping_final_function_offset,
                runtime_olympus_airstepping_latent_linkage_offset,
                runtime_olympus_airstepping_latent_resume_offset,
                runtime_olympus_airstepping_tutorial_continuation_offset,
                runtime_olympus_airstepping_continuation_opcode,
                runtime_olympus_airstepping_bypass_continuation_offset,
                runtime_olympus_airstepping_continuation_opcode,
                runtime_olympus_airstepping_message_id_offset,
                runtime_olympus_airstepping_message_id,
                runtime_olympus_airstepping_message_key_offset,
                runtime_olympus_airstepping_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_casting_magic{
                runtime_olympus_early_tutorial_he02_script_size,
                runtime_olympus_casting_magic_bypass_entry_offset,
                ex_local_variable,
                runtime_olympus_casting_magic_tutorial_callsite_offset,
                runtime_olympus_casting_magic_final_function_offset,
                runtime_olympus_casting_magic_latent_linkage_offset,
                runtime_olympus_casting_magic_latent_resume_offset,
                runtime_olympus_casting_magic_tutorial_continuation_offset,
                runtime_olympus_casting_magic_tutorial_continuation_opcode,
                runtime_olympus_casting_magic_bypass_continuation_offset,
                runtime_olympus_casting_magic_bypass_continuation_opcode,
                runtime_olympus_casting_magic_message_id_offset,
                runtime_olympus_casting_magic_message_id,
                runtime_olympus_casting_magic_message_key_offset,
                runtime_olympus_casting_magic_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_free_running{
                runtime_olympus_early_tutorial_he02_script_size,
                runtime_olympus_free_running_bypass_entry_offset,
                ex_local_variable,
                runtime_olympus_free_running_tutorial_callsite_offset,
                runtime_olympus_free_running_final_function_offset,
                runtime_olympus_free_running_latent_linkage_offset,
                runtime_olympus_free_running_latent_resume_offset,
                runtime_olympus_free_running_tutorial_continuation_offset,
                runtime_olympus_free_running_continuation_opcode,
                runtime_olympus_free_running_bypass_continuation_offset,
                runtime_olympus_free_running_continuation_opcode,
                runtime_olympus_free_running_message_id_offset,
                runtime_olympus_free_running_message_id,
                runtime_olympus_free_running_message_key_offset,
                runtime_olympus_free_running_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_flowmotion_help{
                runtime_olympus_early_tutorial_he02_script_size,
                runtime_olympus_flowmotion_help_callsite_offset,
                ex_object_const,
                runtime_olympus_flowmotion_help_callsite_offset,
                runtime_olympus_flowmotion_help_final_function_offset,
                runtime_olympus_flowmotion_help_latent_linkage_offset,
                runtime_olympus_flowmotion_help_continuation_offset,
                runtime_olympus_flowmotion_help_continuation_offset,
                runtime_olympus_flowmotion_help_continuation_opcode,
                runtime_olympus_flowmotion_help_continuation_offset,
                runtime_olympus_flowmotion_help_continuation_opcode,
                runtime_olympus_flowmotion_help_message_id_offset,
                runtime_olympus_flowmotion_help_message_id,
                0,
                {},
                true,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_attractions_help{
                runtime_olympus_early_tutorial_he02_script_size,
                runtime_olympus_attractions_help_callsite_offset,
                ex_object_const,
                runtime_olympus_attractions_help_callsite_offset,
                runtime_olympus_attractions_help_final_function_offset,
                runtime_olympus_attractions_help_latent_linkage_offset,
                runtime_olympus_attractions_help_latent_resume_offset,
                runtime_olympus_attractions_help_followup_offset,
                runtime_olympus_attractions_help_followup_opcode,
                runtime_olympus_attractions_help_bypass_continuation_offset,
                runtime_olympus_attractions_help_bypass_continuation_opcode,
                runtime_olympus_attractions_help_message_id_offset,
                runtime_olympus_attractions_help_message_id,
                0,
                {},
                true,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_flowmotion_tutorial{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_flowmotion_tutorial_callsite_offset,
                ex_object_const,
                runtime_olympus_flowmotion_tutorial_callsite_offset,
                runtime_olympus_flowmotion_tutorial_final_function_offset,
                runtime_olympus_flowmotion_tutorial_latent_linkage_offset,
                runtime_olympus_flowmotion_tutorial_latent_resume_offset,
                runtime_olympus_flowmotion_tutorial_accepted_continuation_offset,
                runtime_olympus_flowmotion_tutorial_accepted_continuation_opcode,
                runtime_olympus_flowmotion_tutorial_declined_continuation_offset,
                runtime_olympus_flowmotion_tutorial_declined_continuation_opcode,
                runtime_olympus_flowmotion_tutorial_message_id_offset,
                runtime_olympus_flowmotion_tutorial_message_id,
                runtime_olympus_flowmotion_tutorial_message_key_offset,
                runtime_olympus_flowmotion_tutorial_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_team_attacks_help{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_team_attacks_help_callsite_offset,
                ex_object_const,
                runtime_olympus_team_attacks_help_callsite_offset,
                runtime_olympus_team_attacks_help_final_function_offset,
                runtime_olympus_team_attacks_help_latent_linkage_offset,
                runtime_olympus_team_attacks_help_latent_resume_offset,
                runtime_olympus_team_attacks_help_fallthrough_offset,
                runtime_olympus_team_attacks_help_fallthrough_opcode,
                runtime_olympus_team_attacks_help_bypass_continuation_offset,
                runtime_olympus_team_attacks_help_bypass_continuation_opcode,
                runtime_olympus_team_attacks_help_message_id_offset,
                runtime_olympus_team_attacks_help_message_id,
                0,
                {},
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_grand_magic_help{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_grand_magic_help_callsite_offset,
                ex_object_const,
                runtime_olympus_grand_magic_help_callsite_offset,
                runtime_olympus_grand_magic_help_final_function_offset,
                runtime_olympus_grand_magic_help_latent_linkage_offset,
                runtime_olympus_grand_magic_help_continuation_offset,
                runtime_olympus_grand_magic_help_continuation_offset,
                runtime_olympus_grand_magic_help_continuation_opcode,
                runtime_olympus_grand_magic_help_continuation_offset,
                runtime_olympus_grand_magic_help_continuation_opcode,
                runtime_olympus_grand_magic_help_message_id_offset,
                runtime_olympus_grand_magic_help_message_id,
                0,
                {},
                true,
                runtime_olympus_grand_magic_help_continuation_target_offset,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_formchange_tutorial{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_formchange_tutorial_callsite_offset,
                ex_object_const,
                runtime_olympus_formchange_tutorial_callsite_offset,
                runtime_olympus_formchange_tutorial_final_function_offset,
                runtime_olympus_formchange_tutorial_latent_linkage_offset,
                runtime_olympus_formchange_tutorial_latent_resume_offset,
                runtime_olympus_formchange_tutorial_accepted_continuation_offset,
                runtime_olympus_formchange_tutorial_accepted_continuation_opcode,
                runtime_olympus_formchange_tutorial_declined_continuation_offset,
                runtime_olympus_formchange_tutorial_declined_continuation_opcode,
                runtime_olympus_formchange_tutorial_message_id_offset,
                runtime_olympus_formchange_tutorial_message_id,
                runtime_olympus_formchange_tutorial_message_key_offset,
                runtime_olympus_formchange_tutorial_message_key,
                false,
                0,
                runtime_olympus_formchange_tutorial_post_help_continuation_offset,
                runtime_olympus_formchange_tutorial_post_help_continuation_opcode,
                {
                        runtime_olympus_formchange_followup_help_callsite_offset,
                        runtime_olympus_formchange_followup_help_final_function_offset,
                        runtime_olympus_formchange_followup_help_latent_linkage_offset,
                        runtime_olympus_formchange_followup_help_latent_resume_offset,
                        runtime_olympus_formchange_followup_help_latent_resume_jump_target,
                        runtime_olympus_formchange_followup_help_message_id_offset,
                        runtime_olympus_formchange_followup_help_message_id,
                },
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_attractions_tutorial{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_attractions_tutorial_callsite_offset,
                ex_object_const,
                runtime_olympus_attractions_tutorial_callsite_offset,
                runtime_olympus_attractions_tutorial_final_function_offset,
                runtime_olympus_attractions_tutorial_latent_linkage_offset,
                runtime_olympus_attractions_tutorial_latent_resume_offset,
                runtime_olympus_attractions_tutorial_accepted_continuation_offset,
                runtime_olympus_attractions_tutorial_accepted_continuation_opcode,
                runtime_olympus_attractions_tutorial_declined_continuation_offset,
                runtime_olympus_attractions_tutorial_declined_continuation_opcode,
                runtime_olympus_attractions_tutorial_message_id_offset,
                runtime_olympus_attractions_tutorial_message_id,
                runtime_olympus_attractions_tutorial_message_key_offset,
                runtime_olympus_attractions_tutorial_message_key,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_save_points{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_save_points_callsite_offset,
                ex_object_const,
                runtime_olympus_save_points_callsite_offset,
                runtime_olympus_save_points_final_function_offset,
                runtime_olympus_save_points_latent_linkage_offset,
                runtime_olympus_save_points_latent_resume_offset,
                runtime_olympus_save_points_tutorial_continuation_offset,
                runtime_olympus_save_points_tutorial_continuation_opcode,
                runtime_olympus_save_points_bypass_continuation_offset,
                runtime_olympus_save_points_bypass_continuation_opcode,
                runtime_olympus_save_points_message_id_offset,
                runtime_olympus_save_points_message_id,
                0,
                {},
        };

        constexpr RuntimeTutorialPopupBypassProfile s_olympus_moogle_shop{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_moogle_shop_callsite_offset,
                ex_object_const,
                runtime_olympus_moogle_shop_callsite_offset,
                runtime_olympus_moogle_shop_final_function_offset,
                runtime_olympus_moogle_shop_latent_linkage_offset,
                runtime_olympus_moogle_shop_latent_resume_offset,
                runtime_olympus_moogle_shop_tutorial_continuation_offset,
                runtime_olympus_moogle_shop_continuation_opcode,
                runtime_olympus_moogle_shop_bypass_continuation_offset,
                runtime_olympus_moogle_shop_continuation_opcode,
                runtime_olympus_moogle_shop_message_id_offset,
                runtime_olympus_moogle_shop_message_id,
                0,
                {},
        };

        constexpr RuntimeTutorialPopupBypassProfile s_toy_box_aerial_recovery{
                runtime_toy_box_aerial_recovery_script_size,
                runtime_toy_box_aerial_recovery_callsite_offset,
                ex_object_const,
                runtime_toy_box_aerial_recovery_callsite_offset,
                runtime_toy_box_aerial_recovery_final_function_offset,
                runtime_toy_box_aerial_recovery_latent_linkage_offset,
                runtime_toy_box_aerial_recovery_latent_resume_offset,
                runtime_toy_box_aerial_recovery_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_aerial_recovery_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_aerial_recovery_message_id_offset,
                runtime_toy_box_aerial_recovery_message_id,
                0,
                {},
                true,
                runtime_toy_box_aerial_recovery_latent_resume_jump_target,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_toy_box_gigas{
                runtime_toy_box_gigas_script_size,
                runtime_toy_box_gigas_callsite_offset,
                ex_object_const,
                runtime_toy_box_gigas_callsite_offset,
                runtime_toy_box_gigas_final_function_offset,
                runtime_toy_box_gigas_latent_linkage_offset,
                runtime_toy_box_gigas_latent_resume_offset,
                runtime_toy_box_gigas_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_gigas_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_gigas_message_id_offset,
                runtime_toy_box_gigas_message_id,
                0,
                {},
                true,
                runtime_toy_box_gigas_latent_resume_jump_target,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_toy_box_mag_assemble{
                runtime_toy_box_mag_assemble_script_size,
                runtime_toy_box_mag_assemble_callsite_offset,
                ex_object_const,
                runtime_toy_box_mag_assemble_callsite_offset,
                runtime_toy_box_mag_assemble_final_function_offset,
                runtime_toy_box_mag_assemble_latent_linkage_offset,
                runtime_toy_box_mag_assemble_latent_resume_offset,
                runtime_toy_box_mag_assemble_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_mag_assemble_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_mag_assemble_message_id_offset,
                runtime_toy_box_mag_assemble_message_id,
                0,
                {},
                true,
                runtime_toy_box_mag_assemble_latent_resume_jump_target,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_toy_box_eight_bit_blast{
                runtime_toy_box_eight_bit_blast_script_size,
                runtime_toy_box_eight_bit_blast_callsite_offset,
                ex_object_const,
                runtime_toy_box_eight_bit_blast_callsite_offset,
                runtime_toy_box_eight_bit_blast_final_function_offset,
                runtime_toy_box_eight_bit_blast_latent_linkage_offset,
                runtime_toy_box_eight_bit_blast_latent_resume_offset,
                runtime_toy_box_eight_bit_blast_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_eight_bit_blast_latent_resume_offset,
                runtime_toy_box_help_latent_resume_opcode,
                runtime_toy_box_eight_bit_blast_message_id_offset,
                runtime_toy_box_eight_bit_blast_message_id,
                0,
                {},
                true,
                runtime_toy_box_eight_bit_blast_latent_resume_jump_target,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_caribbean_sea_spectacle{
                runtime_caribbean_sea_spectacle_script_size,
                runtime_caribbean_sea_spectacle_bypass_entry_offset,
                ex_object_const,
                runtime_caribbean_sea_spectacle_callsite_offset,
                runtime_caribbean_sea_spectacle_final_function_offset,
                runtime_caribbean_sea_spectacle_latent_linkage_offset,
                runtime_caribbean_sea_spectacle_latent_resume_offset,
                runtime_caribbean_sea_spectacle_tutorial_continuation_offset,
                runtime_caribbean_sea_spectacle_context_opcode,
                runtime_caribbean_sea_spectacle_tutorial_continuation_offset,
                runtime_caribbean_sea_spectacle_context_opcode,
                runtime_caribbean_sea_spectacle_message_id_offset,
                runtime_caribbean_sea_spectacle_message_id,
                0,
                {},
        };

        constexpr RuntimeTutorialPopupBypassProfile s_twilight_town_taking_photos{
                runtime_twilight_town_tutorial_script_size,
                runtime_twilight_town_taking_photos_callsite_offset,
                ex_object_const,
                runtime_twilight_town_taking_photos_callsite_offset,
                runtime_twilight_town_taking_photos_final_function_offset,
                runtime_twilight_town_taking_photos_latent_linkage_offset,
                runtime_twilight_town_taking_photos_latent_resume_offset,
                runtime_twilight_town_taking_photos_latent_resume_offset,
                runtime_twilight_town_help_latent_resume_opcode,
                runtime_twilight_town_taking_photos_bypass_continuation_offset,
                ex_context,
                runtime_twilight_town_taking_photos_message_id_offset,
                runtime_twilight_town_taking_photos_message_id,
                0,
                {},
        };

        constexpr RuntimeTutorialPopupBypassProfile s_twilight_town_lucky_emblems{
                runtime_twilight_town_tutorial_script_size,
                runtime_twilight_town_lucky_emblems_callsite_offset,
                ex_object_const,
                runtime_twilight_town_lucky_emblems_callsite_offset,
                runtime_twilight_town_lucky_emblems_final_function_offset,
                runtime_twilight_town_lucky_emblems_latent_linkage_offset,
                runtime_twilight_town_lucky_emblems_latent_resume_offset,
                runtime_twilight_town_lucky_emblems_latent_resume_offset,
                runtime_twilight_town_help_latent_resume_opcode,
                runtime_twilight_town_lucky_emblems_bypass_continuation_offset,
                ex_context,
                runtime_twilight_town_lucky_emblems_message_id_offset,
                runtime_twilight_town_lucky_emblems_message_id,
                0,
                {},
        };

        constexpr RuntimeTutorialPopupBypassProfile s_twilight_town_links{
                runtime_twilight_town_tutorial_script_size,
                runtime_twilight_town_links_callsite_offset,
                ex_object_const,
                runtime_twilight_town_links_callsite_offset,
                runtime_twilight_town_links_final_function_offset,
                runtime_twilight_town_links_latent_linkage_offset,
                runtime_twilight_town_links_latent_resume_offset,
                runtime_twilight_town_links_latent_resume_offset,
                runtime_twilight_town_help_latent_resume_opcode,
                runtime_twilight_town_links_latent_resume_offset,
                runtime_twilight_town_help_latent_resume_opcode,
                runtime_twilight_town_links_message_id_offset,
                runtime_twilight_town_links_message_id,
                0,
                {},
                true,
                runtime_twilight_town_links_latent_resume_jump_target,
        };

        constexpr RuntimeTutorialPopupBypassProfile s_twilight_town_meow_wow{
                runtime_twilight_town_tutorial_script_size,
                runtime_twilight_town_meow_wow_callsite_offset,
                ex_object_const,
                runtime_twilight_town_meow_wow_callsite_offset,
                runtime_twilight_town_meow_wow_final_function_offset,
                runtime_twilight_town_meow_wow_latent_linkage_offset,
                runtime_twilight_town_meow_wow_latent_resume_offset,
                runtime_twilight_town_meow_wow_latent_resume_offset,
                runtime_twilight_town_help_latent_resume_opcode,
                runtime_twilight_town_meow_wow_latent_resume_offset,
                runtime_twilight_town_help_latent_resume_opcode,
                runtime_twilight_town_meow_wow_message_id_offset,
                runtime_twilight_town_meow_wow_message_id,
                0,
                {},
                true,
                runtime_twilight_town_meow_wow_latent_resume_jump_target,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he01_attraction_flow_enable{
                runtime_olympus_he01_gameflow_script_size,
                runtime_olympus_he01_attraction_flow_enable_callsite_offset,
                runtime_olympus_he01_attraction_flow_enable_virtual_function_offset,
                runtime_olympus_he01_attraction_flow_enable_value_offset,
                ex_true,
                runtime_olympus_he01_attraction_flow_enable_continuation_offset,
                runtime_olympus_he01_attraction_flow_enable_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_attraction_flow_enable{
                runtime_olympus_early_tutorial_he02_script_size,
                runtime_olympus_he02_attraction_flow_enable_callsite_offset,
                runtime_olympus_he02_attraction_flow_enable_virtual_function_offset,
                runtime_olympus_he02_attraction_flow_enable_value_offset,
                ex_true,
                runtime_olympus_he02_attraction_flow_enable_continuation_offset,
                runtime_olympus_he02_attraction_flow_enable_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_attraction_flow_tutorial_enable{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_he03_attraction_flow_tutorial_enable_callsite_offset,
                runtime_olympus_he03_attraction_flow_tutorial_enable_virtual_function_offset,
                runtime_olympus_he03_attraction_flow_tutorial_enable_value_offset,
                ex_true,
                runtime_olympus_attractions_tutorial_declined_continuation_offset,
                runtime_olympus_attractions_tutorial_declined_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_attraction_flow_restore{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_he03_attraction_flow_restore_callsite_offset,
                runtime_olympus_he03_attraction_flow_restore_virtual_function_offset,
                runtime_olympus_he03_attraction_flow_restore_value_offset,
                ex_true,
                runtime_olympus_he03_attraction_flow_restore_continuation_offset,
                runtime_olympus_he03_attraction_flow_restore_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he01_style_change_enable{
                runtime_olympus_he01_gameflow_script_size,
                runtime_olympus_he01_style_change_enable_callsite_offset,
                runtime_olympus_he01_style_change_enable_virtual_function_offset,
                runtime_olympus_he01_style_change_enable_value_offset,
                ex_true,
                runtime_olympus_he01_style_change_enable_continuation_offset,
                runtime_olympus_he01_style_change_enable_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_style_change_enable{
                runtime_olympus_early_tutorial_he02_script_size,
                runtime_olympus_he02_style_change_enable_callsite_offset,
                runtime_olympus_he02_style_change_enable_virtual_function_offset,
                runtime_olympus_he02_style_change_enable_value_offset,
                ex_true,
                runtime_olympus_he02_style_change_enable_continuation_offset,
                runtime_olympus_he02_style_change_enable_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_style_change_tutorial_enable{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_he03_style_change_tutorial_enable_callsite_offset,
                runtime_olympus_he03_style_change_tutorial_enable_virtual_function_offset,
                runtime_olympus_he03_style_change_tutorial_enable_value_offset,
                ex_true,
                runtime_olympus_he03_style_change_tutorial_enable_continuation_offset,
                runtime_olympus_he03_style_change_tutorial_enable_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_style_change_restore{
                runtime_olympus_he03_tutorial_script_size,
                runtime_olympus_he03_style_change_restore_callsite_offset,
                runtime_olympus_he03_style_change_restore_virtual_function_offset,
                runtime_olympus_he03_style_change_restore_value_offset,
                ex_true,
                runtime_olympus_he03_style_change_restore_continuation_offset,
                runtime_olympus_he03_style_change_restore_continuation_opcode,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he01_athletic_flow_enable{
                runtime_olympus_he01_gameflow_script_size,
                0x26372,
                0x26388,
                0x26395,
                ex_true,
                0x26397,
                0x06,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he01_shoot_flow_enable{
                runtime_olympus_he01_gameflow_script_size,
                0x263C7,
                0x263DD,
                0x263EA,
                ex_true,
                0x263EC,
                0x06,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he01_magic_enable{
                runtime_olympus_he01_gameflow_script_size,
                0x264C6,
                0x264DC,
                0x264E9,
                ex_true,
                0x264EB,
                0x06,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he01_wall_run_enable{
                runtime_olympus_he01_gameflow_script_size,
                0x2651B,
                0x26531,
                0x2653E,
                ex_true,
                0x26540,
                0x06,
        };

        // ShootFlow(true) at 0xE082 and AthleticFlow(true) at 0x28774 are the
        // entry instructions replaced by the Shotlock and Airstepping tutorial
        // jumps. Including them here would make the patches overlap.
        constexpr std::array<RuntimeAbilitySetBypassProfile, 6>
                s_olympus_he01_tutorial_ability_sets{
                        s_olympus_he01_athletic_flow_enable,
                        s_olympus_he01_shoot_flow_enable,
                        s_olympus_he01_attraction_flow_enable,
                        s_olympus_he01_style_change_enable,
                        s_olympus_he01_magic_enable,
                        s_olympus_he01_wall_run_enable,
                };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_wall_run_tutorial_disable{
                runtime_olympus_early_tutorial_he02_script_size,
                0x16284,
                0x1629A,
                0x162A7,
                ex_false,
                0x162A9,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_magic_tutorial_enable{
                runtime_olympus_early_tutorial_he02_script_size,
                0x162D4,
                0x162EA,
                0x162F7,
                ex_true,
                0x162F9,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_style_change_tutorial_disable{
                runtime_olympus_early_tutorial_he02_script_size,
                0x16324,
                0x1633A,
                0x16347,
                ex_false,
                0x16349,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_attraction_flow_tutorial_disable{
                runtime_olympus_early_tutorial_he02_script_size,
                0x16374,
                0x1638A,
                0x16397,
                ex_false,
                0x16399,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_shoot_flow_tutorial_disable{
                runtime_olympus_early_tutorial_he02_script_size,
                0x163C4,
                0x163DA,
                0x163E7,
                ex_false,
                0x163E9,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_athletic_flow_restore{
                runtime_olympus_early_tutorial_he02_script_size,
                0x6F4ED,
                0x6F503,
                0x6F510,
                ex_true,
                0x6F512,
                0x06,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_shoot_flow_restore{
                runtime_olympus_early_tutorial_he02_script_size,
                0x6F542,
                0x6F558,
                0x6F565,
                ex_true,
                0x6F567,
                0x06,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_magic_restore{
                runtime_olympus_early_tutorial_he02_script_size,
                0x6F641,
                0x6F657,
                0x6F664,
                ex_true,
                0x6F666,
                0x06,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he02_wall_run_restore{
                runtime_olympus_early_tutorial_he02_script_size,
                0x6F696,
                0x6F6AC,
                0x6F6B9,
                ex_true,
                0x6F6BB,
                0x06,
        };

        // AthleticFlow(false) at 0x16414 and WallRun(true) at 0x70849 are the
        // entry instructions replaced by the Casting Magic and Free Running
        // tutorial jumps. Including them here would make the patches overlap.
        constexpr std::array<RuntimeAbilitySetBypassProfile, 11>
                s_olympus_he02_tutorial_ability_sets{
                        s_olympus_he02_wall_run_tutorial_disable,
                        s_olympus_he02_magic_tutorial_enable,
                        s_olympus_he02_style_change_tutorial_disable,
                        s_olympus_he02_attraction_flow_tutorial_disable,
                        s_olympus_he02_shoot_flow_tutorial_disable,
                        s_olympus_he02_athletic_flow_restore,
                        s_olympus_he02_shoot_flow_restore,
                        s_olympus_he02_attraction_flow_enable,
                        s_olympus_he02_style_change_enable,
                        s_olympus_he02_magic_restore,
                        s_olympus_he02_wall_run_restore,
                };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_wall_run_restore{
                runtime_olympus_he03_tutorial_script_size,
                0x27EF7,
                0x27F0D,
                0x27F1A,
                ex_true,
                0x27F1C,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_magic_restore{
                runtime_olympus_he03_tutorial_script_size,
                0x27F47,
                0x27F5D,
                0x27F6A,
                ex_true,
                0x27F6C,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_shoot_flow_disable{
                runtime_olympus_he03_tutorial_script_size,
                0x28037,
                0x2804D,
                0x2805A,
                ex_false,
                0x2805C,
                0x5F,
        };

        constexpr RuntimeAbilitySetBypassProfile s_olympus_he03_athletic_flow_disable{
                runtime_olympus_he03_tutorial_script_size,
                0x28087,
                0x2809D,
                0x280AA,
                ex_false,
                0x280AC,
                0x14,
        };

        constexpr std::array<RuntimeAbilitySetBypassProfile, 8>
                s_olympus_he03_tutorial_ability_sets{
                        s_olympus_he03_style_change_tutorial_enable,
                        s_olympus_he03_attraction_flow_tutorial_enable,
                        s_olympus_he03_wall_run_restore,
                        s_olympus_he03_magic_restore,
                        s_olympus_he03_style_change_restore,
                        s_olympus_he03_attraction_flow_restore,
                        s_olympus_he03_shoot_flow_disable,
                        s_olympus_he03_athletic_flow_disable,
                };

        [[nodiscard]] constexpr auto encoded_offset_instruction(
                uint8_t opcode,
                size_t target) noexcept -> std::array<uint8_t, 5>
        {
            return {
                    opcode,
                    static_cast<uint8_t>(target & 0xFF),
                    static_cast<uint8_t>((target >> 8) & 0xFF),
                    static_cast<uint8_t>((target >> 16) & 0xFF),
                    static_cast<uint8_t>((target >> 24) & 0xFF),
            };
        }

        template <size_t PatternSize>
        [[nodiscard]] auto matches_at(
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

        [[nodiscard]] auto matches_string_const_at(
                const uint8_t* script,
                size_t script_size,
                size_t offset,
                std::string_view expected) noexcept -> bool
        {
            const size_t encoded_size = 1 + expected.size() + 1;
            return script &&
                   offset <= script_size &&
                   encoded_size <= script_size - offset &&
                   script[offset] == ex_string_const &&
                   std::equal(
                           expected.begin(),
                           expected.end(),
                           script + offset + 1) &&
                   script[offset + 1 + expected.size()] == 0;
        }

        [[nodiscard]] auto byte_available(
                const uint8_t* script,
                size_t script_size,
                size_t offset) noexcept -> bool
        {
            return script && offset < script_size;
        }

        template <size_t ProfileCount>
        auto try_patch_runtime_ability_set_batch(
                uint8_t* script,
                size_t script_size,
                const std::array<RuntimeAbilitySetBypassProfile, ProfileCount>& profiles) noexcept
                -> RuntimeTutorialPopupBypassResult
        {
            RuntimeTutorialPopupBypassResult batch_result =
                    RuntimeTutorialPopupBypassResult::AlreadyPatched;
            for (const RuntimeAbilitySetBypassProfile& profile : profiles)
            {
                const RuntimeTutorialPopupBypassResult result =
                        try_patch_runtime_ability_set_bytecode(
                                script,
                                script_size,
                                profile);
                if (result == RuntimeTutorialPopupBypassResult::Rejected)
                {
                    return result;
                }
                if (result == RuntimeTutorialPopupBypassResult::Patched)
                {
                    batch_result = result;
                }
            }
            return batch_result;
        }
    }

    auto try_patch_runtime_tutorial_popup_bytecode(
            uint8_t* script,
            size_t script_size,
            const RuntimeTutorialPopupBypassProfile& profile) noexcept
            -> RuntimeTutorialPopupBypassResult
    {
        if (!script ||
            script_size != profile.script_size ||
            profile.latent_resume_offset > std::numeric_limits<uint32_t>::max() ||
            profile.tutorial_continuation_offset > std::numeric_limits<uint32_t>::max() ||
            profile.bypass_continuation_offset > std::numeric_limits<uint32_t>::max() ||
            profile.patched_bypass_continuation_offset >
                    std::numeric_limits<uint32_t>::max() ||
            profile.latent_resume_direct_jump_target >
                    std::numeric_limits<uint32_t>::max() ||
            profile.bypass_followup_help.latent_resume_offset >
                    std::numeric_limits<uint32_t>::max() ||
            profile.bypass_followup_help.latent_resume_jump_target >
                    std::numeric_limits<uint32_t>::max())
        {
            return RuntimeTutorialPopupBypassResult::Rejected;
        }

        const size_t patched_bypass_continuation_offset =
                profile.patched_bypass_continuation_offset != 0
                ? profile.patched_bypass_continuation_offset
                : profile.bypass_continuation_offset;
        const auto patched_callsite = encoded_offset_instruction(
                ex_jump,
                patched_bypass_continuation_offset);
        const bool has_original_callsite =
                byte_available(script, script_size, profile.bypass_entry_offset + 1) &&
                script[profile.bypass_entry_offset] == ex_context &&
                script[profile.bypass_entry_offset + 1] ==
                        profile.bypass_entry_object_opcode;
        const bool has_patched_callsite = matches_at(
                script,
                script_size,
                profile.bypass_entry_offset,
                patched_callsite);
        const auto expected_linkage = encoded_offset_instruction(
                ex_skip_offset_const,
                profile.latent_resume_offset);
        const auto expected_bypass_branch = encoded_offset_instruction(
                ex_jump_if_not,
                profile.bypass_continuation_offset);
        const auto expected_direct_resume_jump = encoded_offset_instruction(
                ex_jump_if_not,
                profile.latent_resume_direct_jump_target);
        const auto expected_followup_linkage = encoded_offset_instruction(
                ex_skip_offset_const,
                profile.bypass_followup_help.latent_resume_offset);
        const auto expected_followup_resume_jump = encoded_offset_instruction(
                ex_jump_if_not,
                profile.bypass_followup_help.latent_resume_jump_target);
        const bool has_expected_direct_resume =
                profile.latent_resume_offset ==
                        profile.tutorial_continuation_offset &&
                byte_available(
                        script,
                        script_size,
                        profile.latent_resume_offset) &&
                script[profile.latent_resume_offset] ==
                        profile.tutorial_continuation_opcode &&
                (profile.latent_resume_direct_jump_target == 0 ||
                 (matches_at(
                          script,
                          script_size,
                          profile.latent_resume_offset,
                          expected_direct_resume_jump) &&
                  byte_available(
                          script,
                          script_size,
                          profile.latent_resume_offset +
                                  expected_direct_resume_jump.size()) &&
                  script[profile.latent_resume_offset +
                         expected_direct_resume_jump.size()] == ex_true));
        const bool has_expected_latent_resume =
                profile.latent_resume_has_no_bypass_branch
                ? has_expected_direct_resume
                : matches_at(
                          script,
                          script_size,
                          profile.latent_resume_offset,
                          expected_bypass_branch);
        const bool has_followup_help_anchor =
                profile.bypass_followup_help.callsite_offset != 0 ||
                profile.bypass_followup_help.final_function_offset != 0 ||
                profile.bypass_followup_help.latent_linkage_offset != 0 ||
                profile.bypass_followup_help.latent_resume_offset != 0 ||
                profile.bypass_followup_help.latent_resume_jump_target != 0 ||
                profile.bypass_followup_help.message_id_offset != 0;
        const bool has_expected_followup_help =
                !has_followup_help_anchor ||
                (profile.bypass_followup_help.callsite_offset != 0 &&
                 byte_available(
                         script,
                         script_size,
                         profile.bypass_followup_help.callsite_offset + 1) &&
                 script[profile.bypass_followup_help.callsite_offset] == ex_context &&
                 script[profile.bypass_followup_help.callsite_offset + 1] ==
                         ex_object_const &&
                 byte_available(
                         script,
                         script_size,
                         profile.bypass_followup_help.final_function_offset) &&
                 script[profile.bypass_followup_help.final_function_offset] ==
                         ex_final_function &&
                 matches_at(
                         script,
                         script_size,
                         profile.bypass_followup_help.latent_linkage_offset,
                         expected_followup_linkage) &&
                 matches_at(
                         script,
                         script_size,
                         profile.bypass_followup_help.latent_resume_offset,
                         expected_followup_resume_jump) &&
                 byte_available(
                         script,
                         script_size,
                         profile.bypass_followup_help.latent_resume_offset +
                                 expected_followup_resume_jump.size()) &&
                 script[profile.bypass_followup_help.latent_resume_offset +
                        expected_followup_resume_jump.size()] == ex_true &&
                 matches_at(
                         script,
                         script_size,
                         profile.bypass_followup_help.message_id_offset,
                         std::array<uint8_t, 2>{
                                 ex_byte_const,
                                 profile.bypass_followup_help.message_id,
                         }));
        const std::array<uint8_t, 2> expected_message_id{
                ex_byte_const,
                profile.message_id,
        };

        if (has_original_callsite == has_patched_callsite ||
            (profile.tutorial_callsite_offset != profile.bypass_entry_offset &&
             (!byte_available(script, script_size, profile.tutorial_callsite_offset + 1) ||
              script[profile.tutorial_callsite_offset] != ex_context ||
              script[profile.tutorial_callsite_offset + 1] != ex_object_const)) ||
            !byte_available(script, script_size, profile.final_function_offset) ||
            script[profile.final_function_offset] != ex_final_function ||
            !matches_at(
                    script,
                    script_size,
                    profile.latent_linkage_offset,
                    expected_linkage) ||
            !has_expected_latent_resume ||
            !has_expected_followup_help ||
            !byte_available(script, script_size, profile.tutorial_continuation_offset) ||
            script[profile.tutorial_continuation_offset] !=
                    profile.tutorial_continuation_opcode ||
            !byte_available(script, script_size, profile.bypass_continuation_offset) ||
            script[profile.bypass_continuation_offset] !=
                    profile.bypass_continuation_opcode ||
            (profile.patched_bypass_continuation_offset != 0 &&
             (!byte_available(
                      script,
                      script_size,
                      profile.patched_bypass_continuation_offset) ||
              script[profile.patched_bypass_continuation_offset] !=
                      profile.patched_bypass_continuation_opcode)) ||
            !matches_at(
                    script,
                    script_size,
                    profile.message_id_offset,
                    expected_message_id) ||
            (!profile.message_key.empty() &&
             !matches_string_const_at(
                     script,
                     script_size,
                     profile.message_key_offset,
                     profile.message_key)))
        {
            return RuntimeTutorialPopupBypassResult::Rejected;
        }

        if (has_patched_callsite)
        {
            return RuntimeTutorialPopupBypassResult::AlreadyPatched;
        }

        std::copy(
                patched_callsite.begin(),
                patched_callsite.end(),
                script + profile.bypass_entry_offset);
        return matches_at(
                script,
                script_size,
                profile.bypass_entry_offset,
                patched_callsite)
                ? RuntimeTutorialPopupBypassResult::Patched
                : RuntimeTutorialPopupBypassResult::Rejected;
    }

    auto try_patch_runtime_ability_set_bytecode(
            uint8_t* script,
            size_t script_size,
            const RuntimeAbilitySetBypassProfile& profile) noexcept
            -> RuntimeTutorialPopupBypassResult
    {
        if (!script ||
            script_size != profile.script_size ||
            (profile.value_opcode != ex_true &&
             profile.value_opcode != ex_false) ||
            profile.continuation_offset > std::numeric_limits<uint32_t>::max())
        {
            return RuntimeTutorialPopupBypassResult::Rejected;
        }

        const auto patched_callsite = encoded_offset_instruction(
                ex_jump,
                profile.continuation_offset);
        const bool has_original_callsite =
                byte_available(script, script_size, profile.callsite_offset + 1) &&
                script[profile.callsite_offset] == ex_context &&
                script[profile.callsite_offset + 1] == ex_local_variable;
        const bool has_patched_callsite = matches_at(
                script,
                script_size,
                profile.callsite_offset,
                patched_callsite);

        if (has_original_callsite == has_patched_callsite ||
            !byte_available(script, script_size, profile.virtual_function_offset) ||
            script[profile.virtual_function_offset] != ex_virtual_function ||
            !byte_available(script, script_size, profile.value_offset + 1) ||
            script[profile.value_offset] != profile.value_opcode ||
            script[profile.value_offset + 1] != ex_end_function_parms ||
            !byte_available(script, script_size, profile.continuation_offset) ||
            script[profile.continuation_offset] != profile.continuation_opcode)
        {
            return RuntimeTutorialPopupBypassResult::Rejected;
        }

        if (has_patched_callsite)
        {
            return RuntimeTutorialPopupBypassResult::AlreadyPatched;
        }

        std::copy(
                patched_callsite.begin(),
                patched_callsite.end(),
                script + profile.callsite_offset);
        return matches_at(
                script,
                script_size,
                profile.callsite_offset,
                patched_callsite)
                ? RuntimeTutorialPopupBypassResult::Patched
                : RuntimeTutorialPopupBypassResult::Rejected;
    }

    auto try_patch_runtime_final_world_ability_disables_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        if (!script ||
            (script_size != runtime_final_world_combat_basics_ew01_script_size &&
             script_size != runtime_final_world_combat_basics_ew04_script_size))
            return RuntimeTutorialPopupBypassResult::Rejected;

        // Four vanilla setup branches disable ShootFlow, Magic and StyleChange.
        // Also skip their initial restriction=true calls. Keep restriction=false
        // cleanup, other abilities, cinematics and story progression untouched.
        constexpr std::array<size_t, 15> ew01_callsites{
                15013, 15093, 15173, 28593, 28673, 28753,
                58174, 58254, 58334, 81766, 81846, 81926,
                80566, 81366, 81046};
        // The GoA first-entry portal uses Re Mind's EW04 transition gameflow,
        // even though some streamed assets still have EW01 names.
        constexpr std::array<size_t, 15> ew04_callsites{
                16278, 16358, 16438, 29395, 29475, 29555,
                58292, 58372, 58452, 84629, 84709, 84789,
                83429, 84229, 83909};
        const auto& callsites = script_size == runtime_final_world_combat_basics_ew01_script_size
                ? ew01_callsites : ew04_callsites;
        try
        {
            // Validate the whole batch before publishing any change to live code.
            std::vector<uint8_t> staged(script, script + script_size);
            auto result = RuntimeTutorialPopupBypassResult::AlreadyPatched;
            for (size_t index = 0; index < callsites.size(); ++index)
            {
                const size_t offset = callsites[index];
                const RuntimeAbilitySetBypassProfile profile{
                        script_size, offset, offset + 22, offset + 35,
                        index < 12 ? ex_false : ex_true,
                        offset + 37, 0x5F}; // next EX_LetObj controller lookup
                const auto current = try_patch_runtime_ability_set_bytecode(
                        staged.data(), staged.size(), profile);
                if (current == RuntimeTutorialPopupBypassResult::Rejected) return current;
                if (current == RuntimeTutorialPopupBypassResult::Patched) result = current;
            }
            if (result == RuntimeTutorialPopupBypassResult::Patched)
                for (const size_t offset : callsites)
                    std::copy_n(staged.data() + offset, 5, script + offset);
            return result;
        }
        catch (...)
        {
            return RuntimeTutorialPopupBypassResult::Rejected;
        }
    }

    auto try_patch_runtime_final_world_combat_basics_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        if (script_size == s_final_world_combat_basics_ew01.script_size)
        {
            return try_patch_runtime_tutorial_popup_bytecode(
                    script,
                    script_size,
                    s_final_world_combat_basics_ew01);
        }
        if (script_size == s_final_world_combat_basics_ew04.script_size)
        {
            return try_patch_runtime_tutorial_popup_bytecode(
                    script,
                    script_size,
                    s_final_world_combat_basics_ew04);
        }
        return RuntimeTutorialPopupBypassResult::Rejected;
    }

    auto try_patch_runtime_olympus_shotlock_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_shotlock);
    }

    auto try_patch_runtime_olympus_he01_team_attacks_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_he01_team_attacks_help);
    }

    auto try_patch_runtime_olympus_airstepping_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_airstepping);
    }

    auto try_patch_runtime_olympus_casting_magic_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_casting_magic);
    }

    auto try_patch_runtime_olympus_free_running_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_free_running);
    }

    auto try_patch_runtime_olympus_flowmotion_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_flowmotion_help);
    }

    auto try_patch_runtime_olympus_attractions_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_attractions_help);
    }

    auto try_patch_runtime_olympus_flowmotion_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_flowmotion_tutorial);
    }

    auto try_patch_runtime_olympus_team_attacks_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_team_attacks_help);
    }

    auto try_patch_runtime_olympus_attractions_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_attractions_tutorial);
    }

    auto try_patch_runtime_olympus_grand_magic_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_grand_magic_help);
    }

    auto try_patch_runtime_olympus_formchange_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_formchange_tutorial);
    }

    auto try_patch_runtime_olympus_save_points_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_save_points);
    }

    auto try_patch_runtime_olympus_moogle_shop_tutorial_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_olympus_moogle_shop);
    }

    auto try_patch_runtime_toy_box_aerial_recovery_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_toy_box_aerial_recovery);
    }

    auto try_patch_runtime_toy_box_gigas_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_toy_box_gigas);
    }

    auto try_patch_runtime_toy_box_mag_assemble_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_toy_box_mag_assemble);
    }

    auto try_patch_runtime_toy_box_eight_bit_blast_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_toy_box_eight_bit_blast);
    }

    auto try_patch_runtime_caribbean_sea_spectacle_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_caribbean_sea_spectacle);
    }

    auto try_patch_runtime_twilight_town_taking_photos_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_twilight_town_taking_photos);
    }

    auto try_patch_runtime_twilight_town_lucky_emblems_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_twilight_town_lucky_emblems);
    }

    auto try_patch_runtime_twilight_town_links_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_twilight_town_links);
    }

    auto try_patch_runtime_twilight_town_meow_wow_help_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_tutorial_popup_bytecode(
                script,
                script_size,
                s_twilight_town_meow_wow);
    }

    auto try_patch_runtime_olympus_he02_attraction_flow_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he02_attraction_flow_enable);
    }

    auto try_patch_runtime_olympus_he01_attraction_flow_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he01_attraction_flow_enable);
    }

    auto try_patch_runtime_olympus_he03_attraction_flow_tutorial_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he03_attraction_flow_tutorial_enable);
    }

    auto try_patch_runtime_olympus_he03_attraction_flow_restore_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he03_attraction_flow_restore);
    }

    auto try_patch_runtime_olympus_he01_style_change_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he01_style_change_enable);
    }

    auto try_patch_runtime_olympus_he02_style_change_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he02_style_change_enable);
    }

    auto try_patch_runtime_olympus_he03_style_change_tutorial_enable_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he03_style_change_tutorial_enable);
    }

    auto try_patch_runtime_olympus_he03_style_change_restore_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_bytecode(
                script,
                script_size,
                s_olympus_he03_style_change_restore);
    }

    auto try_patch_runtime_olympus_he01_tutorial_ability_sets_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_batch(
                script,
                script_size,
                s_olympus_he01_tutorial_ability_sets);
    }

    auto try_patch_runtime_olympus_he02_tutorial_ability_sets_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_batch(
                script,
                script_size,
                s_olympus_he02_tutorial_ability_sets);
    }

    auto try_patch_runtime_olympus_he03_tutorial_ability_sets_bytecode(
            uint8_t* script,
            size_t script_size) noexcept -> RuntimeTutorialPopupBypassResult
    {
        return try_patch_runtime_ability_set_batch(
                script,
                script_size,
                s_olympus_he03_tutorial_ability_sets);
    }
}
