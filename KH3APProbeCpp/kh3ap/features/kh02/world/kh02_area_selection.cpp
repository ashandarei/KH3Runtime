#include "kh02_area_selection.h"

namespace kh3ap
{
    auto kh02_open_area_selection(Kh02ChallengeLog& log, RC::Unreal::UObject* menu, const Kh02VisitedFlag& visited,
                                  std::function<void(int)> selected) -> bool
    {
        auto selection = kh02_area_selection(visited);
        auto rows = std::move(selection.rows);
        return log.open_selection(menu, L"Area Select", std::move(rows),
                                  [selection = std::move(selection), selected = std::move(selected)](int index)
                                  {
                                      if (const auto area = selection.selected_area(index))
                                      {
                                          selected(*area);
                                      }
                                  });
    }
} // namespace kh3ap
