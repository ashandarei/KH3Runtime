#pragma once
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include "kh02_area_resume.h"
#include "kh02/runtime_metadata.h"
#include <functional>
#include <optional>

namespace kh3ap
{
    using Kh02VisitedFlag = std::function<int(const std::wstring&)>;

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
