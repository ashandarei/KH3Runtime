#pragma once
#include <array>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    namespace kh02_native_doors
    {
        struct Route
        {
            std::wstring_view source_prefix;
            std::wstring_view actor_name;
            std::wstring_view source_map;
            std::wstring_view destination_map;
            std::wstring_view target_tag;
            int area;
        };

        inline constexpr std::array<Route, 3> routes{
            {{L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.", L"TresMapJump_dw025a_Before",
              L"/Game/Zevels/dw/dw_59/dw_59", L"/Game/Maps/APKH02Mirrors/APKH02Transition", L"dw_59_Lv_Start_01", 8},
             {L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.", L"TresMapJump_dw025a_After",
              L"/Game/Zevels/dw/dw_09/dw_09", L"/Game/Maps/APKH02Mirrors/APKH02Depths", L"dw_09_Lv_Start_01", 8},
             {L"/Game/Zevels/dw/dw_09/umap/dw_09_gameflow.", L"TresMapJumpDirectionalTriggerBox_1",
              L"/Game/Zevels/dw/dw_10/dw_10", L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands", L"dw_10_Lv_Start_01",
              9}}};

        inline auto find(std::wstring_view actor_path, std::wstring_view actor_name) -> const Route*
        {
            for (const auto& route : routes)
            {
                if (actor_path.starts_with(route.source_prefix) && actor_name == route.actor_name)
                {
                    return &route;
                }
            }
            return nullptr;
        }
    } // namespace kh02_native_doors

    // False means this is not an imported native door. Matched doors require
    // verified world/map/tag identity before changing their serialized FName.
    auto kh02_remap_native_door(RC::Unreal::UObject* actor) -> bool;
} // namespace kh3ap
