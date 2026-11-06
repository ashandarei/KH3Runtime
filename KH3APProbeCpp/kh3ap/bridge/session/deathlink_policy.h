#pragma once

#include <string_view>

namespace kh3ap::deathlink
{
    template <typename Char>
    constexpr auto is_station_of_awakening(std::basic_string_view<Char> context) -> bool
    {
        constexpr std::string_view map = "stationofawakening";
        for (size_t offset = 0; offset + map.size() <= context.size(); ++offset)
        {
            bool matches = true;
            for (size_t index = 0; index < map.size(); ++index)
            {
                auto value = context[offset + index];
                if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
                if (value != map[index]) { matches = false; break; }
            }
            if (matches) return true;
        }
        return false;
    }
}
