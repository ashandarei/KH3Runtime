#pragma once

#include <cstddef>
#include <string_view>

namespace kh3ap
{
    constexpr auto runtime_ap_ascii_lower(wchar_t value) -> wchar_t
    {
        return value >= L'A' && value <= L'Z'
                ? static_cast<wchar_t>(value + (L'a' - L'A'))
                : value;
    }

    constexpr auto runtime_ap_contains_ascii_case_insensitive(
            std::wstring_view value,
            std::wstring_view needle) -> bool
    {
        if (needle.empty() || value.size() < needle.size())
        {
            return false;
        }

        for (size_t offset = 0; offset + needle.size() <= value.size(); ++offset)
        {
            bool matched = true;
            for (size_t index = 0; index < needle.size(); ++index)
            {
                if (runtime_ap_ascii_lower(value[offset + index]) !=
                    runtime_ap_ascii_lower(needle[index]))
                {
                    matched = false;
                    break;
                }
            }
            if (matched)
            {
                return true;
            }
        }
        return false;
    }

    // yt_50 is a Toy Box transition/cutscene level that can surface stale
    // controllers while KH3 is chaining an unrelated world transition. It is
    // never a valid AP gameplay context, so no AP runtime subsystem should use
    // its frame, controller, or pawn objects.
    constexpr auto is_runtime_ap_ignored_map_context(std::wstring_view value) -> bool
    {
        return runtime_ap_contains_ascii_case_insensitive(
                       value,
                       L"/Game/Levels/yt/yt_50/") ||
               runtime_ap_contains_ascii_case_insensitive(
                       value,
                       L"/yt/cutscene_yt50_");
    }

    static_assert(is_runtime_ap_ignored_map_context(
            L"World /Game/Levels/yt/yt_50/yt_50.yt_50"));
    static_assert(is_runtime_ap_ignored_map_context(
            L"/Game/Cutscene/Localization/ja/yt/cutscene_yt50_yt021"));
    static_assert(!is_runtime_ap_ignored_map_context(
            L"World /Game/Levels/yt/yt_05/yt_05.yt_05"));
    static_assert(!is_runtime_ap_ignored_map_context(
            L"World /Game/Levels/yt/yt_500/yt_500.yt_500"));
    static_assert(!is_runtime_ap_ignored_map_context(
            L"World /Game/Levels/he/he_50/he_50.he_50"));
}
