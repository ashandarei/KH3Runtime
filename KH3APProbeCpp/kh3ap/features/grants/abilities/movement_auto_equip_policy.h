#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    namespace movement_ability_layout
    {
        inline constexpr uint32_t first_copy_equipped_mask = 0x3;
        inline constexpr uint32_t second_copy_equipped_mask = 0x30;
        inline constexpr uint32_t third_copy_equipped_mask = 0x300;
        inline constexpr uint32_t first_copy_source_bit = 0x8u;
        inline constexpr uint32_t second_copy_source_bit = 0x80u;
        inline constexpr uint32_t third_copy_source_bit = 0x800u;
        inline constexpr int source_to_equipped_bit_shift = 2;
        inline constexpr int vbonus_equipped_count_shift = 18;
        inline constexpr uint32_t vbonus_equipped_count_mask = 0x7f;
    } // namespace movement_ability_layout

    constexpr auto is_auto_equip_movement_ability(std::wstring_view token) -> bool
    {
        for (const auto candidate :
             std::array{L"ETresAbilityKind::GLIDE", L"ETresAbilityKind::AIRSLIDE",
                        L"ETresAbilityKind::HIGHJUMP", L"ETresAbilityKind::DOUBLEFLIGHT",
                        L"ETresAbilityKind::SUPERJUMP", L"ETresAbilityKind::SUPERSLIDE",
                        L"ETresAbilityKind::DODGE", L"ETresAbilityKind::POLE_SPIN",
                        L"ETresAbilityKind::POLE_SWING"})
        {
            if (token == candidate)
            {
                return true;
            }
        }
        return false;
    }

    // Same count used by TresChrBaseParam's native ability query. The low
    // three nibbles are menu copies; the upper count holds equipped VBonuses.
    constexpr auto equipped_ability_copies(uint32_t entry) -> uint32_t
    {
        return ((entry & movement_ability_layout::first_copy_equipped_mask) ==
                movement_ability_layout::first_copy_equipped_mask) +
               ((entry & movement_ability_layout::second_copy_equipped_mask) ==
                movement_ability_layout::second_copy_equipped_mask) +
               ((entry & movement_ability_layout::third_copy_equipped_mask) ==
                movement_ability_layout::third_copy_equipped_mask) +
               ((entry >> movement_ability_layout::vbonus_equipped_count_shift) &
                movement_ability_layout::vbonus_equipped_count_mask);
    }

    constexpr auto movement_auto_equip_mask(uint32_t entry, int32_t total_ap, int32_t used_ap,
                                            uint8_t cost, uint8_t maximum_copies) -> uint32_t
    {
        if (total_ap < 0 || used_ap < 0 || used_ap > total_ap || cost > total_ap - used_ap ||
            equipped_ability_copies(entry) >= maximum_copies)
        {
            return 0;
        }
        for (const uint32_t source : {movement_ability_layout::first_copy_source_bit,
                                      movement_ability_layout::second_copy_source_bit,
                                      movement_ability_layout::third_copy_source_bit})
        {
            if ((entry & source) == 0)
            {
                return source >> movement_ability_layout::source_to_equipped_bit_shift;
            }
        }
        return 0;
    }
} // namespace kh3ap
