#pragma once
#include <string>
#include <string_view>

namespace kh3ap::kh02rewards
{
    inline auto reward_notice(std::wstring_view method, std::wstring_view item) -> std::wstring
    {
        auto message = std::wstring{L"Received "} + std::wstring{item};
        if (method != L"controller_unlock")
        {
            message += L" for Sora";
        }
        return message;
    }
} // namespace kh3ap::kh02rewards
