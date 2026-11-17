#pragma once
#include <string>
#include <string_view>

namespace kh3ap::kh02rewards
{
    inline auto reward_notice(std::wstring_view item) -> std::wstring
    {
        return std::wstring{L"Received "} + std::wstring{item};
    }
} // namespace kh3ap::kh02rewards
