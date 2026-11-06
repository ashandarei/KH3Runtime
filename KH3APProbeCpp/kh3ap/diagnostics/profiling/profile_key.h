#pragma once

#include <functional>
#include <string_view>

namespace kh3ap
{
    struct RuntimeProfileKeyHash
    {
        using is_transparent = void;
        auto operator()(std::wstring_view key) const noexcept -> size_t
        {
            return std::hash<std::wstring_view>{}(key);
        }
    };
}
