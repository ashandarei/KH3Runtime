#pragma once
#include <string_view>

namespace kh3ap
{
    // The source names these layers After_clearing, but AP chests must be
    // available on the first visit, independently of chapter completion.
    inline constexpr auto kh02_postclear_treasure_package(std::wstring_view world) -> std::wstring_view
    {
        if (world == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
        {
            return L"/Game/Zevels/dw/dw_02/umap/dw_02_treasure_After_clearing";
        }
        if (world == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
        {
            return L"/Game/Zevels/dw/dw_03/umap/dw_03_treasure_After_clearing";
        }
        if (world == L"/Game/Maps/APKH02Mirrors/APKH02Stairs.APKH02Stairs")
        {
            return L"/Game/Zevels/dw/dw_04/umap/dw_04_treasure_After_clearing";
        }
        if (world == L"/Game/Maps/APKH02Mirrors/APKH02Corridor.APKH02Corridor")
        {
            return L"/Game/Zevels/dw/dw_05/umap/dw_05_treasure_After_clearing";
        }
        if (world == L"/Game/Maps/APKH02Mirrors/APKH02Tunnel.APKH02Tunnel")
        {
            return L"/Game/Zevels/dw/dw_06/umap/dw_06_treasure_After_clearing";
        }
        if (world == L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest")
        {
            return L"/Game/Zevels/dw/dw_08/umap/dw_08_treasure_After_clearing";
        }
        if (world == L"/Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths")
        {
            return L"/Game/Zevels/dw/dw_09/umap/dw_09_treasure_After_clearing";
        }
        return {};
    }
} // namespace kh3ap
