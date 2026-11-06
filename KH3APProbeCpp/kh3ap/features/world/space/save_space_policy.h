#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace kh3ap::space
{
    // A native faded map jump can be deferred and re-enter the routing hook.
    // Permit only this source/map/start combination during that bounded operation.
    struct SaveMenuReturn
    {
        using Clock = std::chrono::steady_clock;
        static constexpr auto return_timeout = std::chrono::seconds{10};
        static constexpr std::wstring_view return_map = L"/Game/Levels/wm/wm_01/wm_01";
        static constexpr std::wstring_view return_tag = L"wm_01_start01";

        std::wstring source;
        Clock::time_point deadline{};

        void arm(std::wstring_view world, Clock::time_point now)
        {
            source = world;
            deadline = now + return_timeout;
        }

        void clear()
        {
            source.clear();
            deadline = {};
        }

        void observe(std::wstring_view world, Clock::time_point now)
        {
            if (world != source || now >= deadline)
            {
                clear();
            }
        }

        bool allows(std::wstring_view world, std::wstring_view map,
                    std::wstring_view tag, Clock::time_point now) const
        {
            return !source.empty() && world == source && now < deadline &&
                   map == return_map && tag == return_tag;
        }
    };
}
