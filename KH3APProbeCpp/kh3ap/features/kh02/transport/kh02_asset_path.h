#pragma once
#include <cstddef>
#include <string_view>

namespace kh02asset
{
    // Catalog paths contain ASCII only. Match the engine's slash/case variants
    // directly in a string view rather than allocating a normalized copy.
    constexpr wchar_t path_character(wchar_t value)
    {
        if (value == L'\\')
        {
            return L'/';
        }
        if (value >= L'A' && value <= L'Z')
        {
            return value + (L'a' - L'A');
        }
        return value;
    }

    struct AssetPathEqual
    {
        using is_transparent = void;

        constexpr bool operator()(std::wstring_view first, std::wstring_view second) const
        {
            if (first.size() != second.size())
            {
                return false;
            }
            for (size_t index = 0; index < first.size(); ++index)
            {
                if (path_character(first[index]) != path_character(second[index]))
                {
                    return false;
                }
            }
            return true;
        }
    };

    struct AssetPathHash
    {
        using is_transparent = void;

        size_t operator()(std::wstring_view path) const
        {
            // FNV-1a over canonical characters; equivalent path spellings must
            // have the same hash for heterogeneous unordered_map lookup.
            size_t hash = static_cast<size_t>(14695981039346656037ull);
            for (const auto character : path)
            {
                hash ^= static_cast<size_t>(path_character(character));
                hash *= static_cast<size_t>(1099511628211ull);
            }
            return hash;
        }
    };

    constexpr auto content_path_suffix(std::wstring_view path) -> std::wstring_view
    {
        constexpr std::wstring_view marker = L"/content/";
        if (path.size() < marker.size())
        {
            return {};
        }
        // Preserve the original rfind behavior when the prefix also contains
        // a Content directory. No dot-segment or separator collapsing is done.
        for (size_t remaining = path.size() - marker.size() + 1; remaining > 0; --remaining)
        {
            const auto offset = remaining - 1;
            if (path_character(path[offset]) == L'/' && AssetPathEqual{}(path.substr(offset, marker.size()), marker))
            {
                return path.substr(offset);
            }
        }
        return {};
    }
} // namespace kh02asset
