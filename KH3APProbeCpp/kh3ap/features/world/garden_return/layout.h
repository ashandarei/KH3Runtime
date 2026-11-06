#pragma once
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <Unreal/NameTypes.hpp>
#include <array>
#include <chrono>
#include <cstdint>
namespace kh3ap::garden_return_detail
{
    inline constexpr auto default_locomotion_test_swaps = 4;
    inline constexpr auto default_locomotion_test_warmup_ms = 1000;
    inline constexpr auto default_locomotion_test_strength_percent = 100;

    inline constexpr auto camp_third_busy_flag_delta = 2;

    inline constexpr auto caribbean_return_blocked_story_state = 3600;
    inline constexpr auto map_jump_minimum_parameter_bytes = 17;
    inline constexpr auto map_jump_maximum_parameter_bytes = 24;
    inline constexpr auto camp_controller_enabled_flags_offset = 0x60;
    inline constexpr auto camp_rows_offset = 0x380;
    inline constexpr auto steam_camp_close_rva = 0x15c4870;
    inline constexpr auto egs_camp_close_rva = 0x1925fa0;
    inline constexpr auto egs_camp_input_rva = 0x19238a0;
    inline constexpr auto egs_camp_controller_vtable_rva = 0x7c2a488;
    inline constexpr auto egs_camp_menu_vtable_rva = 0x7c45698;
    inline constexpr auto egs_camp_submenu_offset = 0x468;
    inline constexpr auto egs_camp_busy_offset = 0x470;
    inline constexpr auto steam_camp_input_rva = 0x15c5270;
    inline constexpr auto steam_camp_controller_vtable_rva = 0x6a43610;
    inline constexpr auto steam_camp_menu_vtable_rva = 0x6a7d2e0;
    inline constexpr auto steam_camp_submenu_offset = 0x470;
    inline constexpr auto steam_camp_busy_offset = 0x478;
    inline constexpr auto max_camp_reflected_parameter_bytes = 32;
    inline constexpr auto max_camp_pause_parameter_bytes = 256;
    inline constexpr auto max_character_capture_parameter_bytes = 256;
    inline constexpr auto game_instance_pause_type_offset = 0x8d0;
    inline constexpr auto max_ship_chart_parameter_bytes = 64;
    inline constexpr auto max_combat_test_duration_ms = 10000;
    inline constexpr auto max_locomotion_test_swaps = 8;
    inline constexpr auto max_locomotion_test_warmup_ms = 2500;
    inline constexpr auto maximum_locomotion_test_strength_percent = 100;
    inline constexpr auto wardrobe_request_timeout_seconds = 8;
    inline constexpr auto character_request_timeout_seconds = 5;
    inline constexpr auto max_challenge_scroll_releases = 128;
    inline constexpr auto challenge_input_repeat_event = 2;
    inline constexpr auto camp_latent_parameter_bytes = 32;
    inline constexpr auto camp_latent_info_offset = 8;

    inline constexpr auto flag_query_result_offset = sizeof(RC::Unreal::FName);
    inline constexpr auto flag_boolean_query_parameter_bytes =
        sizeof(RC::Unreal::FName) + sizeof(bool);
    inline constexpr auto flag_integer_query_parameter_bytes =
        sizeof(RC::Unreal::FName) + sizeof(int32_t);
    inline constexpr auto flag_query_storage_bytes = 16;

    inline constexpr auto flag_comparison_result_offset =
        sizeof(RC::Unreal::FName) + sizeof(RC::Unreal::FName);
    inline constexpr auto flag_comparison_parameter_bytes =
        flag_comparison_result_offset + sizeof(bool);
    inline constexpr auto flag_comparison_storage_bytes = 24;

    inline constexpr auto stock_camp_row_count = 9;
    inline constexpr auto expanded_camp_row_count = 10;
    inline constexpr auto last_stock_camp_row_index = stock_camp_row_count - 1;
    inline constexpr auto camp_first_row_anchor = 101;
    inline constexpr auto camp_last_stock_row_anchor = 109;
    inline constexpr auto camp_row_top = -408.0;
    inline constexpr auto camp_row_area_height = 768.0;
    inline constexpr auto expanded_camp_row_scale = 0.89;
    inline constexpr auto max_leviathan_chart_candidates = 16;
    inline constexpr int sora_player_id = player_id_value(PlayerId::Sora);
    inline constexpr int aqua_player_id = player_id_value(PlayerId::Aqua);

    inline constexpr auto save_button_hold_duration = std::chrono::milliseconds(150);
    inline constexpr auto phone_button_hold_duration = std::chrono::milliseconds(100);
    inline constexpr auto garden_return_selection_timeout = std::chrono::seconds(10);

    using Clock = std::chrono::steady_clock;
    inline constexpr int32_t garden_anchor = 1901;
    inline constexpr wchar_t garden_button[] = L"kh3apReturnToGarden";
    inline constexpr GfxProbeFunctionPaths gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                               L"/Script/ScaleformUI.GFxObject:Invoke",
                                               L"/Script/ScaleformUI.GFxObject:SetMemberValue"};

} // namespace kh3ap::garden_return_detail
