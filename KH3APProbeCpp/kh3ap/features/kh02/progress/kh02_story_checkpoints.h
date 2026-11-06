#pragma once

namespace kh3ap::kh02_checkpoint
{
    // Native gameflow_DW values. Scene suffixes match the authored flag labels.
    inline constexpr int gear_scene_dw009 = 100;
    inline constexpr int gear_scene_dw010 = 101;
    inline constexpr int gear_scene_dw011 = 102;
    inline constexpr int gear_scene_dw012 = 110;
    inline constexpr int first_gear = 120;
    inline constexpr int second_gear = 130;
    inline constexpr int third_gear = 140;
    inline constexpr int fourth_gear = 150;
    inline constexpr int castle_tower_start = 160;
    inline constexpr int castle_tower_end = 161;
    inline constexpr int gear_scene_dw013 = 170;
    inline constexpr int gear_scene_dw014 = 180;
    inline constexpr int mirror_scene_dw015 = 200;
    inline constexpr int mirror_scene_dw016 = 210;
    inline constexpr int mirror_scene_dw017 = 220;
    inline constexpr int phantom_battle_one = 230;
    inline constexpr int mirror_scene_dw018 = 240;
    inline constexpr int phantom_battle_two = 250;
    inline constexpr int phantom_battle_three = 260;
    inline constexpr int mirror_scene_dw019 = 270;
    inline constexpr int mirror_scene_dw020 = 280;
    inline constexpr int forest_scene_dw021 = 300;
    inline constexpr int forest_scene_dw022 = 310;
    inline constexpr int first_darkside_start = 320;
    inline constexpr int first_darkside_end = 321;
    inline constexpr int forest_area_f_start = 322;
    inline constexpr int forest_area_f_end = 323;
    inline constexpr int forest_scene_dw023 = 330;
    inline constexpr int second_darkside_start = 340;
    inline constexpr int second_darkside_end = 341;
    inline constexpr int forest_scene_dw024 = 350;
    inline constexpr int forest_scene_dw025_a = 360;
    inline constexpr int forest_scene_dw025_b = 361;
    inline constexpr int depths_tower_start = 400;
    inline constexpr int depths_tower_end = 401;
    inline constexpr int forest_scene_dw026 = 410;
    inline constexpr int island_scene_dw027 = 500;
    inline constexpr int island_scene_dw028 = 510;
    inline constexpr int demon_tide_start = 520;
    inline constexpr int demon_tide_end = 521;
    inline constexpr int island_scene_dw029 = 530;
    inline constexpr int island_scene_dw030 = 600;
    inline constexpr int island_scene_dw031 = 610;
    inline constexpr int island_scene_dw032 = 620;
    inline constexpr int island_scene_dw033 = 630;
    inline constexpr int island_scene_dw034 = 700;
    inline constexpr int island_scene_dw035 = 710;
    inline constexpr int magic_room_scene_yt036 = 800;
    inline constexpr int cockpit_scene_gm037 = 810;
    inline constexpr int story_end = 9999;
    inline constexpr int mirror_checkpoint_step = 10;
} // namespace kh3ap::kh02_checkpoint

namespace kh3ap::kh02_gear_counter
{
    // The native flag encodes each collected gear as a ten-point step.
    inline constexpr int one_collected = 10;
    inline constexpr int two_collected = 20;
    inline constexpr int three_collected = 30;
    inline constexpr int all_collected = 40;
} // namespace kh3ap::kh02_gear_counter
