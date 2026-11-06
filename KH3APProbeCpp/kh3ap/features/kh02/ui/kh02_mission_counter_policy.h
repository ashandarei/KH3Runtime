#pragma once

namespace kh3ap
{
    inline constexpr int kh02_default_mission_counter_limit = 5;
    inline constexpr int kh02_maximum_mission_counter_limit = 99;

    constexpr auto kh02_mission_counter_valid(int value, int limit) -> bool
    {
        return value >= 0 && limit >= 1 && limit <= kh02_maximum_mission_counter_limit && value <= limit;
    }

    struct Kh02MissionCounterPresentation
    {
        bool requested{}, visible{}, dirty{};

        void request(bool show)
        {
            requested = show;
            dirty = show;
        }

        void suspend()
        {
            visible = false;
            dirty = requested;
        }

        void presented()
        {
            visible = true;
            dirty = false;
        }
    };
} // namespace kh3ap
