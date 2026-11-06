#pragma once
#include <span>
#include <string>
#include <string_view>

namespace kh3ap
{
    struct Kh02SubtitleCue
    {
        float time{}, length{};
        std::wstring label;
    };

    inline auto kh02_subtitle_culture_matches(std::wstring_view selected, std::wstring_view culture) -> bool
    {
        return !culture.empty() &&
               (selected == culture || (selected.starts_with(culture) && selected.size() > culture.size() &&
                                        selected[culture.size()] == L'-'));
    }

    inline auto kh02_subtitle_at(std::span<const Kh02SubtitleCue> keys, float position) -> int
    {
        int selected = -1;
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            const auto& key = keys[index];
            if (position >= key.time && position <= key.time + key.length)
            {
                selected = static_cast<int>(index);
            }
        }
        return selected;
    }
} // namespace kh3ap
