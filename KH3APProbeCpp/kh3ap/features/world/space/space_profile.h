#pragma once
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/world/space/space_egs_profile.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <type_traits>

namespace kh3ap::space
{
    // Both supported native menu builders use the same display-record layout.
    inline constexpr int32_t mission_display_record_stride = 96;

    // Explicit build profiles. Keep EGS constants as the original baseline;
    // Steam addresses were checked via registration, disassembly and layouts.
    struct NativeProfile
    {
        bool supported{}, steam{}, refresh_is_tick{};
        // Travel-point BeginPlay and saved discovery storage, verified in both images.
        uintptr_t travel_begin_rva{0x12d63b0};
        uintptr_t travel_discovery_flags{0x8c2d8}; // GameInstance + 0x870 + 0x8ba68
        // clang-format off: retain native signature byte grouping
        std::array<uint8_t,24> travel_begin_bytes{0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0x90,0x48,0x81,0xec,0x70,0x01,0x00,0x00};
        // clang-format on
        uintptr_t flight_controller_vtable{0x79bc470}, flight_input_rva{0x49e1a90};
        // clang-format off: retain native signature byte grouping
        std::array<uint8_t,24> flight_input_bytes{0x48,0x8b,0xc4,0x48,0x89,0x50,0x10,0x56,0x57,0x41,0x56,0x48,0x81,0xec,0x00,0x01,0x00,0x00,0x48,0xc7,0x44,0x24,0x50,0xfe};
        // clang-format on
        uintptr_t menu_input_rva{0x1a38900}, choice_input_rva{0x1a49950};
        uintptr_t embark_help_rva{egs_cl1065258::embark_help_rva};
        uintptr_t embark_focus_help_rva{egs_cl1065258::embark_focus_help_rva};
        uintptr_t embark_selector_rva{egs_cl1065258::embark_selector_rva};
        uintptr_t embark_input_rva{egs_cl1065258::embark_input_rva};
        std::remove_cv_t<decltype(egs_cl1065258::embark_help_bytes)> embark_help_bytes{
            egs_cl1065258::embark_help_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::embark_focus_help_bytes)> embark_focus_help_bytes{
            egs_cl1065258::embark_focus_help_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::embark_selector_bytes)> embark_selector_bytes{
            egs_cl1065258::embark_selector_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::embark_input_bytes)> embark_input_bytes{
            egs_cl1065258::embark_input_bytes};
        uintptr_t mission_list_rva{egs_cl1065258::mission_list_rva};
        uintptr_t mission_text_namespace{egs_cl1065258::mission_text_namespace};
        uintptr_t text_lookup_rva{egs_cl1065258::text_lookup_rva};
        uintptr_t text_format_number_rva{egs_cl1065258::text_format_number_rva};
        uintptr_t string_assign_rva{egs_cl1065258::string_assign_rva};
        uintptr_t text_formatter_offset{0x608};
        int32_t mission_menu_state{0x25};
        uintptr_t message_vtable{0x7c5b4f0}, message_input_rva{0x1a4b540};
        // clang-format off: retain native signature byte grouping
        std::array<uint8_t,8> message_input_bytes{0x48,0x8b,0xc4,0x57,0x48,0x81,0xec,0x80};
        // clang-format on
        std::remove_cv_t<decltype(egs_cl1065258::mission_list_bytes)> mission_list_bytes{
            egs_cl1065258::mission_list_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::text_lookup_bytes)> text_lookup_bytes{
            egs_cl1065258::text_lookup_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::text_format_number_bytes)>
            text_format_number_bytes{egs_cl1065258::text_format_number_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::string_assign_bytes)> string_assign_bytes{
            egs_cl1065258::string_assign_bytes};
        uintptr_t overview_default_rva{egs_cl1065258::overview_default_rva};
        uintptr_t overview_current_kind{egs_cl1065258::overview_current_kind};
        uintptr_t overview_current_id{egs_cl1065258::overview_current_id};
        uintptr_t overview_remembered_kinds{egs_cl1065258::overview_remembered_kinds};
        uintptr_t overview_remembered_ids{egs_cl1065258::overview_remembered_ids};
        uintptr_t first_embark_rva{egs_cl1065258::first_embark_rva};
        uintptr_t save_menu_rebuild_rva{egs_cl1065258::save_menu_rebuild_rva};
        uintptr_t landing_list_rva{egs_cl1065258::landing_list_rva};
        uintptr_t image_timestamp{egs_cl1065258::image_timestamp};
        uintptr_t image_size{egs_cl1065258::image_size};
        uintptr_t save_rva{egs_cl1065258::save_rva};
        uintptr_t jump_rva{egs_cl1065258::jump_rva};
        uintptr_t landing_rva{egs_cl1065258::landing_rva};
        uintptr_t lock_rva{egs_cl1065258::lock_rva};
        uintptr_t mission_rva{egs_cl1065258::mission_rva};
        uintptr_t overview_rva{egs_cl1065258::overview_rva};
        uintptr_t icon_state_rva{egs_cl1065258::icon_state_rva};
        uintptr_t travel_query_rva{egs_cl1065258::travel_query_rva};
        uintptr_t refresh_overview_rva{egs_cl1065258::refresh_overview_rva};
        uintptr_t mission_completion_caller_rva{egs_cl1065258::mission_completion_caller_rva};
        uintptr_t overview_state{egs_cl1065258::overview_state};
        uintptr_t overview_idle_state{egs_cl1065258::overview_idle_state};
        uintptr_t overview_region{egs_cl1065258::overview_region};
        uintptr_t overview_region_ids{egs_cl1065258::overview_region_ids};
        uintptr_t icon_kind{egs_cl1065258::icon_kind};
        uintptr_t icon_travel_id{egs_cl1065258::icon_travel_id};
        uintptr_t icon_map{egs_cl1065258::icon_map};
        uintptr_t icon_tag{egs_cl1065258::icon_tag};
        uintptr_t icon_world_id{egs_cl1065258::icon_world_id};
        uintptr_t cursor_kind{egs_cl1065258::cursor_kind};
        uintptr_t cursor_id{egs_cl1065258::cursor_id};
        uintptr_t cursor_visible{egs_cl1065258::cursor_visible};
        uintptr_t cursor_available{egs_cl1065258::cursor_available};
        uintptr_t cursor_selectable{egs_cl1065258::cursor_selectable};
        uintptr_t overview_anchor{egs_cl1065258::overview_anchor};
        uintptr_t overview_world_states{egs_cl1065258::overview_world_states};
        uintptr_t overview_region_states{egs_cl1065258::overview_region_states};
        uintptr_t overview_region_count{egs_cl1065258::overview_region_count};
        uintptr_t game_instance_pause_state{egs_cl1065258::game_instance_pause_state};
        uintptr_t overview_cursor_data{egs_cl1065258::overview_cursor_data};
        uintptr_t overview_cursor_count{egs_cl1065258::overview_cursor_count};
        uintptr_t overview_selected_cursor{egs_cl1065258::overview_selected_cursor};
        uintptr_t cursor_stride{egs_cl1065258::cursor_stride};
        uintptr_t battle_end_rva{egs_cl1065258::battle_end_rva};
        uintptr_t weak_assign_rva{egs_cl1065258::weak_assign_rva};
        uintptr_t table_row_struct{egs_cl1065258::table_row_struct};
        uintptr_t table_rows{egs_cl1065258::table_rows};
        uintptr_t table_free_count{egs_cl1065258::table_free_count};
        uintptr_t battle_result{egs_cl1065258::battle_result};
        uintptr_t battle_individual{egs_cl1065258::battle_individual};
        uintptr_t mission_flags{egs_cl1065258::mission_flags};
        // SetTreasureSphereOpened and the live TresGameInstance bitset.
        uintptr_t sphere_open_rva{0x1b9df80}, sphere_open_flags{0x8c2da};
        // clang-format off: retain native signature byte grouping
        std::array<uint8_t,24> sphere_open_bytes{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x1d,0x4f,0x3f,0x73,0x07,0x0f,0xb6,0xf9,0x48,0x85,0xdb,0x0f};
        // clang-format on
        uintptr_t ui_register_panel_rva{egs_cl1065258::ui_register_panel_rva};
        uintptr_t hud_load_rva{egs_cl1065258::hud_load_rva};
        uintptr_t hud_close_rva{egs_cl1065258::hud_close_rva};
        uintptr_t item_catalog_id_rva{egs_cl1065258::item_catalog_id_rva};
        uintptr_t banner_set_item_rva{egs_cl1065258::banner_set_item_rva};
        uintptr_t panel_layer{egs_cl1065258::panel_layer};
        uintptr_t panel_movie{egs_cl1065258::panel_movie};
        uintptr_t banner_animating{egs_cl1065258::banner_animating};
        uintptr_t banner_start_vtable_offset{egs_cl1065258::banner_start_vtable_offset};
        uintptr_t hud_item_visible{egs_cl1065258::hud_item_visible};
        std::remove_cv_t<decltype(egs_cl1065258::overview_default_bytes)> overview_default_bytes{
            egs_cl1065258::overview_default_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::first_embark_bytes)> first_embark_bytes{
            egs_cl1065258::first_embark_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::save_menu_rebuild_bytes)> save_menu_rebuild_bytes{
            egs_cl1065258::save_menu_rebuild_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::landing_list_bytes)> landing_list_bytes{
            egs_cl1065258::landing_list_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::save_bytes)> save_bytes{egs_cl1065258::save_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::jump_bytes)> jump_bytes{egs_cl1065258::jump_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::landing_bytes)> landing_bytes{
            egs_cl1065258::landing_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::lock_bytes)> lock_bytes{egs_cl1065258::lock_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::mission_bytes)> mission_bytes{
            egs_cl1065258::mission_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::overview_bytes)> overview_bytes{
            egs_cl1065258::overview_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::icon_state_bytes)> icon_state_bytes{
            egs_cl1065258::icon_state_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::travel_query_bytes)> travel_query_bytes{
            egs_cl1065258::travel_query_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::refresh_overview_bytes)> refresh_overview_bytes{
            egs_cl1065258::refresh_overview_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::battle_end_bytes)> battle_end_bytes{
            egs_cl1065258::battle_end_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::weak_assign_bytes)> weak_assign_bytes{
            egs_cl1065258::weak_assign_bytes};
        std::remove_cv_t<decltype(egs_cl1065258::notification_signatures)> notification_signatures{
            egs_cl1065258::notification_signatures};
    };

    inline NativeProfile select_native_profile(uint32_t timestamp, uint32_t size)
    {
        NativeProfile p;
        if (timestamp == egs_cl1065258::image_timestamp && size == egs_cl1065258::image_size)
        {
            p.supported = true;
            return p;
        }
        if (!kh3ap::native_build::steam.matches(timestamp, size))
        {
            return p;
        }
        p.supported = p.steam = p.refresh_is_tick = true;
        p.travel_begin_rva = 0x1025c00;
        p.travel_begin_bytes = {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x54, 0x41, 0x55,
                                0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24,
                                0xa0, 0x48, 0x81, 0xec, 0x60, 0x01, 0x00, 0x00};
        // Steam's setter takes (uint8_t id, bool opened), unlike EGS's
        // set-only (uint8_t id). Both use GameInstance + 0x870 + 0x8ba6a.
        p.sphere_open_rva = 0x1809970;
        p.sphere_open_bytes = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83,
                               0xec, 0x20, 0x0f, 0xb6, 0xfa, 0x0f, 0xb6, 0xd9,
                               0xe8, 0x8b, 0x91, 0xd1, 0xff, 0x4c, 0x8b, 0xd0};
        p.flight_controller_vtable = 0x66c41f8;
        p.flight_input_rva = 0x4436010;
        p.flight_input_bytes = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10, 0x55,
                                0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x68, 0xa9,
                                0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00, 0x48};
        p.menu_input_rva = 0x16c7240;
        p.choice_input_rva = 0x16c7110;
        p.embark_help_rva = 0x16c0c10;
        // Steam inlines focus help into the bool(menu, event) listener rather
        // than EGS's void(menu, FString*) helper. Input queries likewise use
        // bool(input, button, filtered), including the inlined Confirm query.
        p.embark_focus_help_rva = 0x16bbdb0;
        p.embark_selector_rva = 0x17d8460;
        p.embark_input_rva = 0x1583b80;
        p.embark_focus_help_bytes = {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x56, 0x41, 0x57,
                                     0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x50, 0x48,
                                     0xc7, 0x45, 0xd0, 0xfe, 0xff, 0xff, 0xff, 0x48};
        p.embark_selector_bytes = {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41, 0x56, 0x48,
                                   0x8d, 0x68, 0xa1, 0x48, 0x81, 0xec, 0x90, 0x00,
                                   0x00, 0x00, 0x48, 0xc7, 0x45, 0xd7, 0xfe, 0xff};
        p.embark_input_bytes = {0x83, 0xfa, 0x1d, 0x7d, 0x1c, 0x48, 0x63, 0xc2,
                                0x48, 0x8d, 0x14, 0x40, 0x45, 0x84, 0xc0, 0x74,
                                0x07, 0x80, 0x7c, 0x91, 0x11, 0x00, 0x75, 0x09};
        // Both builders use the same 96-byte display records. Steam's numeric
        // formatter owns UIManager + 0x670 and takes a fifth nullable argument.
        p.mission_list_rva = 0x16c47d0;
        p.mission_text_namespace = 0x86f0a00;
        p.text_lookup_rva = 0x1580680;
        p.text_format_number_rva = 0x1580140;
        p.string_assign_rva = 0x45eff0;
        p.text_formatter_offset = 0x670;
        p.message_vtable = 0x6b1c838;
        p.message_input_rva = 0x16c7590;
        p.message_input_bytes = {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x83, 0xec, 0x70};
        p.mission_list_bytes = {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x54, 0x41, 0x55,
                                0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xa8, 0x28,
                                0xff, 0xff, 0xff, 0x48, 0x81, 0xec, 0xb0, 0x01};
        p.text_format_number_bytes = {0x4c, 0x8b, 0xdc, 0x55, 0x56, 0x57, 0x48, 0x81,
                                      0xec, 0x90, 0x00, 0x00, 0x00, 0x49, 0xc7, 0x43};
        p.image_timestamp = timestamp;
        p.image_size = size;
        p.overview_default_rva = 0x17d0360;
        p.first_embark_rva = 0x1795ed0;
        p.save_menu_rebuild_rva = 0x1568550;
        p.landing_list_rva = 0x16cc180;
        p.save_rva = 0xe9ff60;
        p.jump_rva = 0xebd300;
        p.landing_rva = 0x1042260;
        p.lock_rva = 0x10450d0;
        p.mission_rva = 0x1042cb0;
        p.overview_rva = 0x17d1580;
        p.icon_state_rva = 0x17ce430;
        p.travel_query_rva = 0x2cdbd50;
        p.refresh_overview_rva = 0x17d90f0;
        p.battle_end_rva = 0x1054ef0;
        p.weak_assign_rva = 0x2f66990;
        p.ui_register_panel_rva = 0x1591b10;
        p.hud_load_rva = 0x163e330;
        p.hud_close_rva = 0x163afa0;
        p.item_catalog_id_rva = 0x112f510;
        p.banner_set_item_rva = 0x1648980;
        p.mission_completion_caller_rva = 0x104bc4e;
        p.overview_default_bytes = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b,
                                    0xd9, 0xc7, 0x81, 0xbc, 0x06, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0xc6, 0x81, 0x60, 0x06, 0x00};
        p.first_embark_bytes = {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
                                0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50,
                                0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff, 0xff};
        p.save_menu_rebuild_bytes = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
                                     0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48,
                                     0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff, 0xff, 0xff};
        p.landing_list_bytes = {0x89, 0x54, 0x24, 0x10, 0x55, 0x57, 0x41, 0x55,
                                0x41, 0x56, 0x48, 0x83, 0xec, 0x28, 0x4c, 0x8d,
                                0xb1, 0x50, 0x04, 0x00, 0x00, 0x8b, 0xfa, 0x41};
        p.save_bytes = {0x48, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
                        0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7,
                        0x44, 0x24, 0x38, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0x9c};
        p.jump_bytes = {0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55,
                        0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8d, 0x6c, 0x24, 0xa0, 0x48,
                        0x81, 0xec, 0x60, 0x01, 0x00, 0x00, 0x48, 0xc7, 0x44, 0x24};
        p.landing_bytes = {0x48, 0x8b, 0xc4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8d,
                           0x6c, 0x24, 0xa0, 0x48, 0x81, 0xec, 0x60, 0x01, 0x00, 0x00, 0x48,
                           0xc7, 0x45, 0xb0, 0xfe, 0xff, 0xff, 0xff, 0x0f, 0x29, 0x70};
        p.lock_bytes = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48,
                        0x8b, 0xd9, 0x0f, 0xb6, 0xfa, 0x48, 0x8b, 0x89, 0xc0, 0x04, 0x00,
                        0x00, 0x48, 0x85, 0xc9, 0x74, 0x08, 0x41, 0xb0, 0x02, 0xe8};
        p.mission_bytes = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48,
                           0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb6,
                           0xf2, 0x49, 0x8b, 0xd8, 0x48, 0x8b, 0xd1, 0x48, 0x8b, 0xe9};
        p.overview_bytes = {0x40, 0x53, 0x48, 0x81, 0xec, 0x60, 0x07, 0x00, 0x00, 0x48, 0x8b,
                            0x05, 0xa8, 0x2a, 0xfe, 0x06, 0x48, 0x33, 0xc4, 0x48, 0x89, 0x84,
                            0x24, 0x38, 0x07, 0x00, 0x00, 0x48, 0x8b, 0xd9, 0xe8, 0x6d};
        p.icon_state_bytes = {0x48, 0x89, 0x5c, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x20,
                              0x44, 0x8b, 0x5a, 0x04, 0x49, 0x8b, 0xd8, 0x45, 0x33, 0xc0};
        p.travel_query_bytes = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x42, 0x20,
                                0x45, 0x33, 0xc9, 0x48, 0x85, 0xc0, 0x49, 0x8b, 0xd8};
        p.refresh_overview_bytes = {0x48, 0x8b, 0xc4, 0x55, 0x56, 0x57, 0x41, 0x54,
                                    0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d};
        p.battle_end_bytes = {0x40, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
                              0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe};
        p.weak_assign_bytes = {0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83,
                               0xec, 0x30, 0x48, 0x8b, 0xf1, 0x48, 0x85, 0xd2};
        p.notification_signatures[0] = {p.ui_register_panel_rva,
                                        {0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20,
                                         0x48, 0x8b, 0xda, 0x48, 0x8b, 0xf9}};
        p.notification_signatures[1] = {p.hud_load_rva,
                                        {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
                                         0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c}};
        p.notification_signatures[2] = {p.hud_close_rva,
                                        {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                         0x48, 0x89, 0x74, 0x24, 0x18, 0x48}};
        p.notification_signatures[3] = {p.item_catalog_id_rva,
                                        {0x48, 0x83, 0x79, 0x40, 0x00, 0x4c, 0x8b, 0xd1, 0x0f, 0x84,
                                         0x98, 0x00, 0x00, 0x00, 0x8b, 0x81}};
        p.notification_signatures[4] = {p.banner_set_item_rva,
                                        {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                         0x41, 0x57, 0x48, 0x8d, 0xac, 0x24}};
        return p;
    }

    inline const NativeProfile& native_profile()
    {
        static const NativeProfile value = []
        {
            const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
            if (!base)
            {
                return NativeProfile{};
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
                dos->e_lfanew >= 0x1000)
            {
                return NativeProfile{};
            }
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (pe->Signature != IMAGE_NT_SIGNATURE)
            {
                return NativeProfile{};
            }
            return select_native_profile(pe->FileHeader.TimeDateStamp,
                                         pe->OptionalHeader.SizeOfImage);
        }();
        return value;
    }
} // namespace kh3ap::space
