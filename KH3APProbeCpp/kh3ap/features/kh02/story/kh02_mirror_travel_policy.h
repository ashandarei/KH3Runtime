#pragma once
#include "kh3ap/features/kh02/core/kh02_world_identity.h"
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace kh3ap::kh02_mirror_travel
{
    struct UnhostedDestination : std::runtime_error
    {
        UnhostedDestination() : std::runtime_error("kh02_mirror_destination_not_hosted") {}
    };

    struct Plan
    {
        std::wstring package, tag;
        int fade{};
        bool special_battle_clear{};
    };

    inline auto resolve(std::wstring_view caller_path, std::wstring_view source_world_path,
                        const std::wstring& destination, const std::wstring& destination_tag, int fade_kind,
                        bool epilogue_complete, bool pending) -> Plan
    {
        constexpr int maximum_source_fade_kind = 3;
        constexpr int root_script_area_begin = 50;
        int source_area = 0;
        for (int area_id : {3, 4, 5, 6, 7, 8, 9, 10, 11, 59, 60})
        {
            auto area = std::wstring(area_id < 10 ? L"0" : L"") + std::to_wstring(area_id);
            if (caller_path.starts_with(L"/Game/Zevels/dw/dw_" + area +
                                        (area_id >= root_script_area_begin ? L"/dw_" : L"/umap/dw_") + area +
                                        (area_id >= root_script_area_begin ? L"." : L"_gameflow.")))
            {
                source_area = area_id;
            }
        }
        const auto* current_room = kh02_world_identity::mirror_room(source_world_path);
        const bool is_hub = source_world_path == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin";
        if (!source_area || (!is_hub && (!current_room || current_room->area != source_area)) ||
            (is_hub && source_area != 3) || fade_kind > maximum_source_fade_kind || pending)
        {
            throw std::runtime_error("kh02_mirror_jump_context");
        }
        std::wstring host_package;
        bool valid_tag = false;
        if (destination == L"/Game/Zevels/dw/dw_03/dw_03")
        {
            host_package = L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin";
            for (const auto& hub_start : kh02mirror_metadata::hub_starts)
            {
                valid_tag |= destination_tag == hub_start.tag;
            }
        }
        else
        {
            for (const auto& room : kh02mirror_metadata::rooms)
            {
                auto area = std::wstring(room.area < 10 ? L"0" : L"") + std::to_wstring(room.area);
                if (destination != L"/Game/Zevels/dw/dw_" + area + L"/dw_" + area)
                {
                    continue;
                }
                host_package = room.package;
                for (int start_index = 0; start_index < room.start_count; ++start_index)
                {
                    valid_tag |= destination_tag == room.starts[start_index].tag;
                }
            }
        }
        const bool epilogue_return = source_area == 60 && destination_tag == L"BattleReport_Lv_Start_01" &&
                                     (destination == L"/Game/Zaps/UI/BattleReport_Beginner" ||
                                      destination == L"/Game/Zaps/UI/BattleReport_Standard" ||
                                      destination == L"/Game/Zaps/UI/BattleReport_Proud");
        if (epilogue_return)
        {
            if (!epilogue_complete)
            {
                throw std::runtime_error("kh02_epilogue_not_complete");
            }
            host_package = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01";
            valid_tag = true;
        }
        if (host_package.empty() || !valid_tag)
        {
            throw UnhostedDestination{};
        }
        const bool special_battle_clear = source_area == 7 && destination_tag == L"dw_03_Lv_ChallengeClear" &&
                                          caller_path.starts_with(L"/Game/Zevels/dw/dw_07/umap/dw_07_gameflow.");
        return {std::move(host_package), epilogue_return ? L"NormalSpawn" : destination_tag, fade_kind,
                special_battle_clear};
    }
} // namespace kh3ap::kh02_mirror_travel
