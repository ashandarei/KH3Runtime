#pragma once
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include <UnrealDef.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::controller_ability_detail
{
    using RC::Unreal::UObject;
    inline constexpr std::wstring_view s_unlock_token_prefix = STR("KH3AP_CONTROLLER_UNLOCK::");
    inline constexpr std::wstring_view s_ability_enum_path =
        STR("/Script/TresGame.ETresAbilityKind");
    namespace ability_id
    {
        inline constexpr uint8_t wall_run = 113;
        inline constexpr uint8_t style_change = 114;
        inline constexpr uint8_t magic = 115;
        inline constexpr uint8_t attraction_flow = 116;
        inline constexpr uint8_t links = 134;
    } // namespace ability_id
    namespace setter_index
    {
        inline constexpr size_t wall_run = 0;
        inline constexpr size_t style_change = 1;
        inline constexpr size_t mp_enabled = 2;
        inline constexpr size_t attraction_flow = 3;
        inline constexpr size_t airstep = 4;
        inline constexpr size_t shoot_flow = 5;
        inline constexpr size_t magic_restriction = 6;
        inline constexpr size_t links_restriction = 7;
        inline constexpr size_t count = links_restriction + 1;
    } // namespace setter_index
    namespace restriction_bits
    {
        inline constexpr uint32_t magic = 0x02;
        inline constexpr uint32_t links = 0x10;
    } // namespace restriction_bits
    namespace enum_layout
    {
        inline constexpr size_t names_offset = 0x58;
        inline constexpr int32_t maximum_names = 2048;
        inline constexpr int32_t maximum_capacity = 4096;
        inline constexpr size_t native_name_pair_bytes = 16;
    } // namespace enum_layout
    inline constexpr size_t ownership_setter_hook_count = setter_index::airstep + 1;
    inline constexpr size_t magic_restriction_hook_index = ownership_setter_hook_count;
    inline constexpr size_t links_restriction_hook_index = magic_restriction_hook_index + 1;

    struct ControllerAbilityDefinition
    {
        std::wstring_view key;
        std::wstring_view enum_name;
        uint8_t ability_value{};
        size_t setter_index{};
    };

    inline constexpr std::array<ControllerAbilityDefinition, 4> s_controller_abilities{{
        {
            STR("wall_run"),
            STR("ETresAbilityKind::AP_WALL_RUN"),
            ability_id::wall_run,
            setter_index::wall_run,
        },
        {
            STR("style_change"),
            STR("ETresAbilityKind::AP_STYLE_CHANGE"),
            ability_id::style_change,
            setter_index::style_change,
        },
        {
            STR("magic"),
            STR("ETresAbilityKind::AP_MAGIC"),
            ability_id::magic,
            setter_index::mp_enabled,
        },
        {
            STR("attraction_flow"),
            STR("ETresAbilityKind::AP_ATTRACTION_FLOW"),
            ability_id::attraction_flow,
            setter_index::attraction_flow,
        },
    }};

    inline constexpr std::array<ControllerAbilityDefinition, 17> s_keyblade_style_abilities{{
        {STR("WEP_KEYBLADE00"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_00"), 117, 0},
        {STR("WEP_KEYBLADE01"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_01"), 118, 0},
        {STR("WEP_KEYBLADE02"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_02"), 119, 0},
        {STR("WEP_KEYBLADE03"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_03"), 120, 0},
        {STR("WEP_KEYBLADE04"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_04"), 121, 0},
        {STR("WEP_KEYBLADE05"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_05"), 122, 0},
        {STR("WEP_KEYBLADE06"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_06"), 123, 0},
        {STR("WEP_KEYBLADE07"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_07"), 124, 0},
        {STR("WEP_KEYBLADE08"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_08"), 125, 0},
        {STR("WEP_KEYBLADE09"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_09"), 126, 0},
        {STR("WEP_KEYBLADE10"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_10"), 127, 0},
        {STR("WEP_KEYBLADE11"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_11"), 128, 0},
        {STR("WEP_KEYBLADE12"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_12"), 129, 0},
        {STR("WEP_KEYBLADE13"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_13"), 130, 0},
        {STR("WEP_KEYBLADE14"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_14"), 131, 0},
        {STR("WEP_KEYBLADE17"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_17"), 132, 0},
        {STR("WEP_KEYBLADE18"), STR("ETresAbilityKind::AP_KEYBLADE_STYLE_18"), 133, 0},
    }};

    // Keep the original four contiguous for their batch read. IDs 117-133
    // already belong to published keyblade style abilities.
    inline constexpr std::array<ControllerAbilityDefinition, 1> s_link_abilities{{
        {STR("links"), STR("ETresAbilityKind::AP_LINKS"), ability_id::links,
         setter_index::links_restriction},
    }};

    // One live range supplies both the controller gates and Magic/Links.
    // Retain caller order while the snapshot helper bridges the style-ID gap.
    inline constexpr auto s_refresh_abilities = []
    {
        std::array<uint8_t, s_controller_abilities.size() + 1> ids{};
        for (size_t index = 0; index < s_controller_abilities.size(); ++index)
        {
            ids[index] = s_controller_abilities[index].ability_value;
        }
        ids.back() = ability_id::links;
        return ids;
    }();
    inline constexpr size_t s_magic_refresh_index =
        ability_id::magic - s_controller_abilities.front().ability_value;
    static_assert(s_refresh_abilities[s_magic_refresh_index] == ability_id::magic);

    static_assert(
        []
        {
            for (size_t index = 0; index < s_controller_abilities.size(); ++index)
            {
                if (s_controller_abilities[index].ability_value !=
                    s_controller_abilities.front().ability_value + index)
                {
                    return false;
                }
            }
            return true;
        }(),
        "Controller ability batch reads require consecutive ability ids");

    auto unlock_key(std::wstring_view runtime_token) -> std::wstring_view;
    auto find_definition(std::wstring_view key) -> const ControllerAbilityDefinition*;
    auto try_read_command_restrictions(UObject* pawn, uint32_t& flags) -> bool;
} // namespace kh3ap::controller_ability_detail
