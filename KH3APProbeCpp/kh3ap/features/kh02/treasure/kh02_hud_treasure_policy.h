#pragma once
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    inline constexpr uint8_t kh02_castle_treasure_area = 1;
    inline constexpr uint8_t kh02_mirror_treasure_area = 2;
    inline constexpr uint8_t kh02_depths_treasure_area = 4;

    inline auto kh02_imported_treasure_area(std::wstring_view identity) -> uint8_t
    {
        const bool depths = identity >= L"DW_BOX026" && identity <= L"DW_BOX030" || identity == L"DW_LBOX04";
        const bool castle = identity == L"DW_BOX002" || identity == L"DW_BOX005" || identity == L"DW_BOX031" ||
                            identity == L"DW_BOX036";
        return depths ? kh02_depths_treasure_area : castle ? kh02_castle_treasure_area : kh02_mirror_treasure_area;
    }
} // namespace kh3ap
