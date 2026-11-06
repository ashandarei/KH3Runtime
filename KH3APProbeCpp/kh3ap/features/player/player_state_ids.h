#pragma once

#include <cstdint>

namespace kh3ap::player_state_ids
{
    // ETresStateID values shared by exploration and the observed-state catalog.
    // An ID here does not imply that the state is safe for item delivery;
    // RuntimeSoraStateCatalogEntry owns that separate policy.
    inline constexpr uint8_t idle = 1;
    inline constexpr uint8_t run = 2;
    inline constexpr uint8_t jump = 3;
    inline constexpr uint8_t glide = 4;
    inline constexpr uint8_t floating = 5;
    inline constexpr uint8_t dangle = 6;
    inline constexpr uint8_t climbing = 7;
    inline constexpr uint8_t wall_run = 8;
    inline constexpr uint8_t hopping = 9;
    inline constexpr uint8_t attack = 12;
    inline constexpr uint8_t guard = 15;
    inline constexpr uint8_t dodge_roll = 16;
    inline constexpr uint8_t fire_magic = 18;
    inline constexpr uint8_t damage = 19;
    inline constexpr uint8_t air_slide = 20;
    inline constexpr uint8_t rail_slide = 22;
    inline constexpr uint8_t wall_kick = 23;
    inline constexpr uint8_t pole_turn = 24;
    inline constexpr uint8_t style_change = 31;
    inline constexpr uint8_t weapon_change = 32;
    inline constexpr uint8_t style_finish = 33;
    inline constexpr uint8_t shoot_flow = 41;
    inline constexpr uint8_t athletic_flow = 42;
    inline constexpr uint8_t die = 59;
    inline constexpr uint8_t vanish_die = 60;
    inline constexpr uint8_t reflect = 66;
    inline constexpr uint8_t cinematic = 67;
    inline constexpr uint8_t open_treasure_box = 74;
    inline constexpr uint8_t combo_gm = 81;
    inline constexpr uint8_t combo_anti = 86;
    inline constexpr uint8_t combo_swim = 89;
    inline constexpr uint8_t fr_mount_curling = 92;
    inline constexpr uint8_t gimmick_attach = 97;
    inline constexpr uint8_t swim = 102;
    inline constexpr uint8_t special_ship = 103;
    inline constexpr uint8_t save_menu = 105;
    inline constexpr uint8_t shop_menu = 106;
    inline constexpr uint8_t check = 108;
    inline constexpr uint8_t use_item = 111;
    inline constexpr uint8_t game_over = 118;
    inline constexpr uint8_t bad_stat_stun = 122;
    inline constexpr uint8_t bad_stat_freeze = 123;
    inline constexpr uint8_t bad_stat_burn = 124;
    inline constexpr uint8_t dm_stitch = 128;
    inline constexpr uint8_t rail_slide_mi = 138;
    inline constexpr uint8_t hold_camera = 144;
    inline constexpr uint8_t festival_dance = 145;
    inline constexpr uint8_t one_action = 150;
} // namespace kh3ap::player_state_ids
