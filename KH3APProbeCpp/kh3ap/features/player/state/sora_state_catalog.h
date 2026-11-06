#pragma once
#include "kh3ap/features/player/player_state_ids.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeSoraStateKind : uint8_t
    {
        Idle,
        Run,
        Jump,
        Glide,
        Dangle,
        Climbing,
        Attack,
        Guard,
        Reflect,
        DodgeRoll,
        FireMagic,
        Damage,
        AirSlide,
        WallKick,
        WallRun,
        StyleChange,
        WeaponChange,
        StyleFinish,
        ShootFlow,
        AthleticFlow,
        Die,
        Cinematic,
        OpenTreasureBox,
        ComboGM,
        ComboAnti,
        ComboSwim,
        FRMountCurling,
        Swim,
        SaveMenu,
        ShopMenu,
        UseItem,
        GameOver,
        BadStatStun,
        BadStatFreeze,
        BadStatBurn,
        DMStitch,
        HoldCamera,
        FestivalDance,
        OneAction,
        RailSlideMI,
    };

    enum class RuntimeSoraStateSafety : uint8_t
    {
        Unknown = 0,
        Ok,
        NotOk,
    };

    struct RuntimeSoraStateCatalogEntry
    {
        RuntimeSoraStateKind kind;
        uint8_t id;
        std::wstring_view class_name;
        RuntimeSoraStateSafety safety;
        std::wstring_view note;
    };

    constexpr std::array<RuntimeSoraStateCatalogEntry, 40> k_observed_sora_state_catalog{{
            {RuntimeSoraStateKind::Idle, player_state_ids::idle, L"TresPlayerState_Idle", RuntimeSoraStateSafety::Ok, L"observed during normal playable idle"},
            {RuntimeSoraStateKind::Run, player_state_ids::run, L"TresPlayerState_Run", RuntimeSoraStateSafety::Ok, L"observed during normal movement"},
            {RuntimeSoraStateKind::Jump, player_state_ids::jump, L"TresPlayerState_Jump", RuntimeSoraStateSafety::Ok, L"observed during normal movement"},
            {RuntimeSoraStateKind::Glide, player_state_ids::glide, L"TresPlayerState_Glide", RuntimeSoraStateSafety::Ok, L"observed during normal gliding"},
            {RuntimeSoraStateKind::Dangle, player_state_ids::dangle, L"TresPlayerState_Dangle", RuntimeSoraStateSafety::Ok, L"observed while hanging/dangling"},
            {RuntimeSoraStateKind::Climbing, player_state_ids::climbing, L"TresPlayerState_Climbing", RuntimeSoraStateSafety::Ok, L"observed while climbing"},
            {RuntimeSoraStateKind::WallRun, player_state_ids::wall_run, L"TresPlayerState_WallRun", RuntimeSoraStateSafety::Ok, L"observed during wall run"},
            {RuntimeSoraStateKind::Attack, player_state_ids::attack, L"TresPlayerState_Attack", RuntimeSoraStateSafety::Ok, L"observed during attack"},
            {RuntimeSoraStateKind::Guard, player_state_ids::guard, L"TresPlayerState_Guard", RuntimeSoraStateSafety::Ok, L"observed while guarding"},
            {RuntimeSoraStateKind::DodgeRoll, player_state_ids::dodge_roll, L"TresPlayerState_DodgeRoll", RuntimeSoraStateSafety::Ok, L"observed during dodge roll"},
            {RuntimeSoraStateKind::FireMagic, player_state_ids::fire_magic, L"TresPlayerState_FireMagic", RuntimeSoraStateSafety::Ok, L"observed during fire magic"},
            {RuntimeSoraStateKind::Damage, player_state_ids::damage, L"TresPlayerState_Damage", RuntimeSoraStateSafety::Ok, L"observed while taking damage"},
            {RuntimeSoraStateKind::AirSlide, player_state_ids::air_slide, L"TresPlayerState_AirSlide", RuntimeSoraStateSafety::Ok, L"observed during air slide"},
            {RuntimeSoraStateKind::WallKick, player_state_ids::wall_kick, L"TresPlayerState_WallKick", RuntimeSoraStateSafety::Ok, L"observed during wall kick"},
            {RuntimeSoraStateKind::StyleChange, player_state_ids::style_change, L"TresPlayerState_StyleChange", RuntimeSoraStateSafety::Ok, L"observed during style change"},
            {RuntimeSoraStateKind::WeaponChange, player_state_ids::weapon_change, L"TresPlayerState_WeaponChange", RuntimeSoraStateSafety::Ok, L"observed during weapon change"},
            {RuntimeSoraStateKind::StyleFinish, player_state_ids::style_finish, L"TresPlayerState_StyleFinish", RuntimeSoraStateSafety::NotOk, L"blocked until tested safe"},
            {RuntimeSoraStateKind::ShootFlow, player_state_ids::shoot_flow, L"TresPlayerState_ShootFlow", RuntimeSoraStateSafety::NotOk, L"blocked during shotlock/flow state"},
            {RuntimeSoraStateKind::AthleticFlow, player_state_ids::athletic_flow, L"TresPlayerState_AthleticFlow", RuntimeSoraStateSafety::NotOk, L"blocked during airstepping (TSID_STATE_ATHLETICFLOW)"},
            {RuntimeSoraStateKind::Die, player_state_ids::die, L"TresPlayerState_Die", RuntimeSoraStateSafety::NotOk, L"blocked while Sora is dying"},
            {RuntimeSoraStateKind::Reflect, player_state_ids::reflect, L"TresPlayerState_Reflect", RuntimeSoraStateSafety::Ok, L"observed during playable reflect transition"},
            {RuntimeSoraStateKind::Cinematic, player_state_ids::cinematic, L"TresCharState_Cinematic", RuntimeSoraStateSafety::NotOk, L"blocked during cinematic/map entry"},
            {RuntimeSoraStateKind::OpenTreasureBox, player_state_ids::open_treasure_box, L"TresPlayerState_OpenTreasureBox", RuntimeSoraStateSafety::NotOk, L"blocked while opening a chest"},
            {RuntimeSoraStateKind::ComboGM, player_state_ids::combo_gm, L"TresPlayerState_ComboGM", RuntimeSoraStateSafety::Ok, L"observed during playable combo state"},
            {RuntimeSoraStateKind::ComboAnti, player_state_ids::combo_anti, L"TresPlayerState_ComboAnti", RuntimeSoraStateSafety::Ok, L"observed during anti/combo state"},
            {RuntimeSoraStateKind::ComboSwim, player_state_ids::combo_swim, L"TresPlayerState_ComboSwim", RuntimeSoraStateSafety::Ok, L"observed during playable swim combo state"},
            {RuntimeSoraStateKind::FRMountCurling, player_state_ids::fr_mount_curling, L"TresPlayerState_FRMountCurling", RuntimeSoraStateSafety::Ok, L"observed during Frozen mountain curling"},
            {RuntimeSoraStateKind::Swim, player_state_ids::swim, L"TresPlayerState_Swim", RuntimeSoraStateSafety::Ok, L"observed during normal swimming"},
            {RuntimeSoraStateKind::SaveMenu, player_state_ids::save_menu, L"TresPlayerState_SaveMenu", RuntimeSoraStateSafety::NotOk, L"blocked in save menu"},
            {RuntimeSoraStateKind::ShopMenu, player_state_ids::shop_menu, L"TresPlayerState_ShopMenu", RuntimeSoraStateSafety::NotOk, L"blocked in shop menu"},
            {RuntimeSoraStateKind::UseItem, player_state_ids::use_item, L"TresPlayerState_UseItem", RuntimeSoraStateSafety::Ok, L"observed while using an item"},
            {RuntimeSoraStateKind::GameOver, player_state_ids::game_over, L"TresPlayerState_GameOver", RuntimeSoraStateSafety::NotOk, L"blocked during game over"},
            {RuntimeSoraStateKind::BadStatStun, player_state_ids::bad_stat_stun, L"TresPlayerState_BadStatStun", RuntimeSoraStateSafety::Ok, L"observed during playable combat stun"},
            {RuntimeSoraStateKind::BadStatFreeze, player_state_ids::bad_stat_freeze, L"TresPlayerState_BadStatFreeze", RuntimeSoraStateSafety::Ok, L"native player bad-status state"},
            {RuntimeSoraStateKind::BadStatBurn, player_state_ids::bad_stat_burn, L"TresPlayerState_BadStatBurn", RuntimeSoraStateSafety::Ok, L"observed during a native Burn hit"},
            {RuntimeSoraStateKind::DMStitch, player_state_ids::dm_stitch, L"TresPlayerState_DMStitch", RuntimeSoraStateSafety::Ok, L"observed during Stitch link state"},
            {RuntimeSoraStateKind::RailSlideMI, player_state_ids::rail_slide_mi, L"TresPlayerState_RailSlideMI", RuntimeSoraStateSafety::Ok, L"allowed during Monstropolis rail sliding"},
            {RuntimeSoraStateKind::HoldCamera, player_state_ids::hold_camera, L"TresPlayerState_HoldCamera", RuntimeSoraStateSafety::NotOk, L"blocked during hold-camera state"},
            {RuntimeSoraStateKind::FestivalDance, player_state_ids::festival_dance, L"TresPlayerState_FestivalDance", RuntimeSoraStateSafety::NotOk, L"blocked during festival dance state"},
            {RuntimeSoraStateKind::OneAction, player_state_ids::one_action, L"TresPlayerState_OneAction", RuntimeSoraStateSafety::Ok, L"observed during playable one-action state"},
    }};

    auto lookup_runtime_sora_state_by_kind(RuntimeSoraStateKind kind) -> const RuntimeSoraStateCatalogEntry*;
    auto lookup_runtime_sora_state_by_id(uint8_t id) -> const RuntimeSoraStateCatalogEntry*;
    auto lookup_runtime_sora_state_by_class_name(std::wstring_view class_name) -> const RuntimeSoraStateCatalogEntry*;
    auto runtime_sora_state_matches_kind(
            RuntimeSoraStateKind kind,
            std::optional<uint8_t> id,
            std::wstring_view class_name) -> bool;
    auto runtime_sora_state_class_name(RuntimeSoraStateKind kind) -> std::wstring_view;
    auto runtime_sora_state_id(RuntimeSoraStateKind kind) -> std::optional<uint8_t>;
    auto runtime_sora_state_safety_label(RuntimeSoraStateSafety safety) -> std::wstring_view;
}
