#pragma once
#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap
{
    inline constexpr auto preferred_player_start_rank = 100;
    inline constexpr auto normal_player_start_rank = 90;
    inline constexpr auto save_player_start_rank = 80;
    inline constexpr auto event_player_start_rank = 5;
    inline constexpr auto numbered_player_start_rank = 70;
    inline constexpr auto generic_player_start_rank = 60;
    inline constexpr auto autosave_player_start_rank = 50;
    inline constexpr auto other_player_start_rank = 10;
    inline constexpr auto map_jump_start_request_timeout = std::chrono::seconds(90);

    inline constexpr std::wstring_view map_jump_auto_start_tag = L"KH3AP_AutoPlayerStart";

    inline auto map_jump_start_rank(std::wstring_view tag, std::wstring_view preferred = {}) -> int
    {
        std::wstring name(tag);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](wchar_t ch) -> wchar_t
                       {
                           return ch >= L'A' && ch <= L'Z'
                                      ? static_cast<wchar_t>(ch + (L'a' - L'A'))
                                      : ch;
                       });
        for (auto unsafe : {L"enemy", L"camera", L"debug", L"test", L"cutscene", L"cinema"})
        {
            if (name.find(unsafe) != std::wstring::npos)
            {
                return -1;
            }
        }
        if (!preferred.empty() && tag == preferred)
        {
            return preferred_player_start_rank;
        }
        if (name == L"normalspawn" || name == L"normalplayerstart" || name == L"playerstart")
        {
            return normal_player_start_rank;
        }
        if (name.find(L"autosave") == std::wstring::npos &&
            name.find(L"save") != std::wstring::npos)
        {
            return save_player_start_rank;
        }
        if (name.find(L"event") != std::wstring::npos ||
            name.find(L"battle") != std::wstring::npos || name.find(L"boss") != std::wstring::npos)
        {
            return event_player_start_rank;
        }
        if (name.find(L"start_01") != std::wstring::npos)
        {
            return numbered_player_start_rank;
        }
        if (name.find(L"start") != std::wstring::npos)
        {
            return generic_player_start_rank;
        }
        if (name.find(L"autosave") != std::wstring::npos)
        {
            return autosave_player_start_rank;
        }
        return name.empty() || name == L"none" ? 0 : other_player_start_rank;
    }

    // A request owns one destination's initial spawn only. No actor is retained
    // across travel, and unrelated worlds/retries cannot inherit the override.
    class MapJumpPlayerStartRequest
    {
    public:
        using Clock = std::chrono::steady_clock;
        void arm(std::wstring package, Clock::time_point now)
        {
            destination = std::move(package);
            expires = now + map_jump_start_request_timeout;
        }
        auto matches(std::wstring_view world, Clock::time_point now) const -> bool
        {
            if (!pending(now))
            {
                return false;
            }
            const auto separator = destination.rfind(L'/');
            return world.size() == destination.size() + 1 + destination.size() - separator - 1 &&
                   world.starts_with(destination) && world[destination.size()] == L'.' &&
                   world.substr(destination.size() + 1) ==
                       std::wstring_view(destination).substr(separator + 1);
        }
        auto pending(Clock::time_point now) const -> bool
        {
            return !destination.empty() && now < expires;
        }
        void clear()
        {
            destination.clear();
        }

    private:
        std::wstring destination;
        Clock::time_point expires{};
    };
} // namespace kh3ap
