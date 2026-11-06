#pragma once
#include <string_view>

namespace kh3ap
{
    inline constexpr auto kh02_postclear_treasure_package(std::wstring_view world, bool chapter_complete)
        -> std::wstring_view
    {
        if (!chapter_complete)
        {
            return {};
        }
        if (world == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
        {
            return L"/Game/Zevels/dw/dw_02/umap/dw_02_treasure_After_clearing";
        }
        if (world == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
        {
            return L"/Game/Zevels/dw/dw_03/umap/dw_03_treasure_After_clearing";
        }
        return {};
    }
} // namespace kh3ap
