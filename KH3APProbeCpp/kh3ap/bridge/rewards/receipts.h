#pragma once
#include <algorithm>
#include <cstdint>
#include <set>

namespace kh3ap::reconciliation
{
    struct Receipts
    {
        int64_t through{-1};
        std::set<int64_t> extra;
        auto contains(int64_t index) const -> bool
        { return index >= 0 && (index <= through || extra.contains(index)); }
        auto add(int64_t index) -> void
        {
            if (index <= through || index < 0) return;
            extra.insert(index);
            while (extra.erase(through + 1)) ++through;
        }
        auto operator==(const Receipts&) const -> bool = default;
        auto remove(int64_t index) -> void
        {
            if (index < 0) return;
            if (index <= through)
            {
                for (auto i = index + 1; i <= through; ++i) extra.insert(i);
                through = index - 1;
            }
            extra.erase(index);
        }
    };
}
