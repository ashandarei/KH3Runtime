#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap::ability_probe_detail
{
    struct EquipmentOffsetProbe
    {
        std::wstring_view name;
        std::wstring_view kind;
        size_t offset;
        int assumed_element_size;
    };

    // Shared layout catalog for broad and targeted TresEquipmentComponent probes.
    // The two consumers retain their distinct log limits and deep-follow policies.
    inline constexpr size_t equipment_attach_pawn_offset = 0x210;
    inline constexpr size_t equipment_attach_skin_offset = 0x218;
    inline constexpr size_t equipment_weapon_equips_offset = 0x240;
    inline constexpr size_t equipment_weapon_set_offset = 0x250;
    inline constexpr size_t equipment_fixed_equips_offset = 0x260;
    inline constexpr size_t equipment_form_ability_asset_offset = 0x278;
    inline constexpr size_t equipment_slots_offset = 0x2E8;
    inline constexpr size_t equipment_base_parameter_offset = 0x380;
    inline constexpr int equipment_struct_probe_bytes = 16;

    inline constexpr int form_ability_data_offset = 0x48;
    inline constexpr int keyblade_equipment_map_offset = 0x50;
    inline constexpr int broad_asset_map_entry_bytes = 40;
    inline constexpr int targeted_form_map_entry_bytes = 48;
    inline constexpr int targeted_equipment_map_entry_bytes = 32;

    inline constexpr std::array<EquipmentOffsetProbe, 9> equipment_offset_probes{{
        {L"m_AttachPawn", L"ManualObject", equipment_attach_pawn_offset, 0},
        {L"m_AttachSkin", L"ManualObject", equipment_attach_skin_offset, 0},
        {L"m_WeaponEquips", L"ManualObjectArray", equipment_weapon_equips_offset, static_cast<int>(sizeof(RC::Unreal::UObject*))},
        {L"MyWeaponSet", L"ManualObject", equipment_weapon_set_offset, 0},
        {L"m_FixedEquips", L"ManualStructArray", equipment_fixed_equips_offset, equipment_struct_probe_bytes},
        {L"m_FormAbilityAsset", L"ManualObject", equipment_form_ability_asset_offset, 0},
        {L"m_EquipSlots", L"ManualStructRaw", equipment_slots_offset, equipment_struct_probe_bytes},
        {L"m_EquipSlots.m_pAsset", L"ManualObject", equipment_slots_offset, 0},
        {L"m_pBaseParam", L"ManualObject", equipment_base_parameter_offset, 0},
    }};
}
