#pragma once
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include "kh02_area_resume.h"
#include "kh02/runtime_metadata.h"
#include <functional>
#include <optional>

namespace kh3ap
{
    using Kh02VisitedFlag = std::function<int(const std::wstring&)>;

    struct Kh02ForestSavePoint
    {
        int destination;
        int checkpoint;
        const wchar_t* title;
        const wchar_t* tag;
    };

    inline constexpr int kh02_forest_area = 8;
    inline constexpr auto kh02_forest_rail_start_tag = L"dw_08_Lv_Autosave_05";
    inline constexpr Kh02ForestSavePoint kh02_forest_save_points[]{
        {801, kh02_checkpoint::forest_scene_dw021, L"Forest of Thorns - Save Point 1", L"dw_08_Lv_Save_01"},
        {804, kh02_checkpoint::forest_area_f_end, L"Forest of Thorns - Replay Rail Slide", kh02_forest_rail_start_tag},
        {802, kh02_checkpoint::forest_area_f_end, L"Forest of Thorns - Save Point 2", L"dw_08_Lv_Save_02"},
        {803, kh02_checkpoint::forest_scene_dw024, L"Forest of Thorns - Save Point 3", L"dw_08_Lv_Save_03"},
    };

    struct Kh02AreaSelection
    {
        std::vector<Kh02ChallengeRow> rows;
        std::vector<int> areas;

        auto selected_area(int index) const -> std::optional<int>
        {
            if (index < 0 || static_cast<std::size_t>(index) >= areas.size())
            {
                return std::nullopt;
            }
            return areas[index];
        }
    };

    inline auto kh02_visited_area_flag(int area) -> std::wstring
    {
        return L"gameflow_DW_VISITED_" + std::to_wstring(area);
    }

    inline auto kh02_forest_save_point(int destination, const Kh02VisitedFlag& visited) -> const Kh02ForestSavePoint*
    {
        if (visited(kh02_visited_area_flag(kh02_forest_area)) != 1)
        {
            return nullptr;
        }
        const int checkpoint = visited(L"gameflow_DW");
        for (const auto& point : kh02_forest_save_points)
        {
            if (destination == point.destination && checkpoint >= point.checkpoint)
            {
                return &point;
            }
        }
        return nullptr;
    }

    inline auto kh02_area_selection(const Kh02VisitedFlag& visited) -> Kh02AreaSelection
    {
        Kh02AreaSelection selection;
        for (const auto& area : kh02metadata::visited_areas)
        {
            if (visited(kh02_visited_area_flag(area.area)) == 1)
            {
                selection.areas.push_back(area.area);
                selection.rows.push_back(
                    {area.title, L"Visited", L"Return to this area. Story and challenge progress are preserved.", 1});
                if (area.area == kh02_forest_area)
                {
                    for (const auto& point : kh02_forest_save_points)
                    {
                        if (kh02_forest_save_point(point.destination, visited))
                        {
                            selection.areas.push_back(point.destination);
                            const auto* description = std::wstring_view(point.tag) == kh02_forest_rail_start_tag
                                                          ? L"Return to the rail approach and retry the challenge."
                                                          : L"Return to this save point.";
                            selection.rows.push_back({point.title, L"Visited", description, 1});
                        }
                    }
                }
            }
        }
        return selection;
    }

    inline auto kh02_visited_area_destination(int number, const Kh02VisitedFlag& visited) -> const kh02metadata::Area*
    {
        if (visited(kh02_visited_area_flag(number)) != 1)
        {
            return nullptr;
        }
        for (const auto& area : kh02metadata::visited_areas)
        {
            if (area.area == number)
            {
                return &area;
            }
        }
        return nullptr;
    }

    auto kh02_open_area_selection(Kh02ChallengeLog& log, RC::Unreal::UObject* menu, const Kh02VisitedFlag& visited,
                                  std::function<void(int)> selected) -> bool;
} // namespace kh3ap
