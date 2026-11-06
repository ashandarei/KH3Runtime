#pragma once
#include "kh02_chest_journal.h"
#include <set>
#include <string>

namespace kh3ap
{
    inline auto kh02_chest_source_id(std::wstring_view id) -> std::string
    {
        // All authored treasure identities are ASCII.
        std::string name;
        name.reserve(id.size());
        for (const auto character : id)
        {
            name.push_back(static_cast<char>(character));
        }
        return name;
    }

    inline auto kh02_chest_collected(std::wstring_view id, int source_flag,
                                     const std::set<std::string>& completed_checks) -> bool
    {
        for (const auto& group : kh02_chest_journal::groups)
        {
            for (std::size_t index = 0; index < group.count; ++index)
            {
                if (group.identities[index] == id)
                {
                    const auto name = kh02_chest_source_id(id);
                    return source_flag == 1 || completed_checks.contains("KH02:Treasure:" + name);
                }
            }
        }
        return false;
    }
} // namespace kh3ap
