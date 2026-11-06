#pragma once
#include "kh3ap/core/gfx/gfx_value_layout.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace kh3ap::now_playing_gfx
{
    inline constexpr std::uint32_t value_type_mask = gfx_native_value::value_type_mask;
    inline constexpr std::uint32_t object_value_type = gfx_native_value::object_value_type;
    inline constexpr std::uint32_t display_object_value_type =
        gfx_native_value::display_object_value_type;

    // Verified against the shipping GetMemberValue native thunk/body. The
    // runtime admits these offsets only after matching both instruction sites.
    inline constexpr std::size_t member_call_offset = 0xcd;
    inline constexpr std::size_t member_fields_offset = 0xd2;
    inline constexpr std::size_t interface_offset = gfx_native_value::interface_offset;
    inline constexpr std::size_t type_offset = gfx_native_value::type_offset;
    inline constexpr std::size_t value_offset = gfx_native_value::value_offset;
    // clang-format off: retain native signature byte grouping
    inline constexpr std::array<std::uint8_t, 16> thunk_prefix{
        0x4c, 0x8b, 0xdc, 0x57, 0x48, 0x83, 0xec, 0x70,
        0x49, 0xc7, 0x43, 0xa8, 0xfe, 0xff, 0xff, 0xff};
    // clang-format on
    // clang-format off: retain native signature byte grouping
    inline constexpr std::array<std::uint8_t, 14> member_call{
        0x4c, 0x8d, 0x44, 0x24, 0x28, 0x48, 0x8d, 0x54, 0x24, 0x38, 0x48, 0x8b, 0xcd, 0xe8};
    // clang-format on
    // clang-format off: retain native signature byte grouping
    inline constexpr std::array<std::uint8_t, 30> member_fields{
        0x8b, 0x47, 0x58, 0x25, 0x8f, 0x00, 0x00, 0x00, 0x3c, 0x0a,
        0x0f, 0x94, 0xc0, 0x88, 0x44, 0x24, 0x20, 0x4c, 0x8d, 0x4c,
        0x24, 0x38, 0x48, 0x8b, 0x57, 0x60, 0x48, 0x8b, 0x4f, 0x50};
    // clang-format on

    template <std::size_t Size>
    inline auto matches(std::span<const std::uint8_t> code, std::size_t offset,
                        const std::array<std::uint8_t, Size>& expected) -> bool
    {
        if (offset > code.size() || expected.size() > code.size() - offset)
        {
            return false;
        }
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            if (code[offset + index] != expected[index])
            {
                return false;
            }
        }
        return true;
    }

    inline auto native_value_ready(std::uintptr_t object_interface, std::uint32_t type,
                                   std::uintptr_t value) -> bool
    {
        // The managed bit may accompany a live object, array or display object.
        // Undefined/Null/scalars have no ObjectInterface usable by GetMember.
        const auto base_type = type & value_type_mask;
        return object_interface != 0 && value != 0 && base_type >= object_value_type &&
               base_type <= display_object_value_type;
    }
} // namespace kh3ap::now_playing_gfx
