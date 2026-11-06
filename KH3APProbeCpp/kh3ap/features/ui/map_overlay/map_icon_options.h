#pragma once
#include <cstdint>
#include <set>
#include <map>

namespace kh3ap
{
    enum class MapIconOption : uint32_t
    {
        Chests = 1,
        Emblems = 2,
        Ingredients = 4,
        Minimap = 8,
        LargeMap = 16,
        LockedChests = 32
    };
    inline constexpr uint32_t map_marker_options =
        static_cast<uint32_t>(MapIconOption::Chests) |
        static_cast<uint32_t>(MapIconOption::Emblems) |
        static_cast<uint32_t>(MapIconOption::Ingredients);
    inline constexpr uint32_t map_icon_defaults =
        map_marker_options | static_cast<uint32_t>(MapIconOption::Minimap) |
        static_cast<uint32_t>(MapIconOption::LargeMap) |
        static_cast<uint32_t>(MapIconOption::LockedChests);
    inline constexpr auto map_icon_enabled(uint32_t options, MapIconOption option) -> bool
    {
        return (options & static_cast<uint32_t>(option)) != 0;
    }
    // Own the complete R2 cycle even if a menu or map closes during the press.
    class MapIconInput
    {
        std::set<int> held_;

    public:
        auto route(int controller, int event, bool eligible, bool& toggle) -> bool
        {
            toggle = false;
            if (held_.contains(controller))
            {
                if (event == 1)
                {
                    held_.erase(controller);
                }
                return true;
            }
            if (event != 0 || !eligible)
            {
                return false;
            }
            held_.insert(controller);
            toggle = true;
            return true;
        }
    };
    // Defer single-trigger zoom until release, allowing an overlapping L2/R2 chord.
    // Keep owned releases captured through map/menu closure and require both up
    // before another chord; different controllers never combine their triggers.
    class MapTriggerInput
    {
        struct Hold
        {
            unsigned held{};
            bool chord{}, cancelled{};
        };
        std::map<int, Hold> controllers_;

    public:
        void cancel_pending()
        {
            for (auto& [id, h] : controllers_)
            {
                h.cancelled = true;
            }
        }
        bool route(int controller, int direction, int event, bool eligible, int& zoom, bool& toggle)
        {
            zoom = 0;
            toggle = false;
            if (direction != -1 && direction != 1)
            {
                return false;
            }
            const unsigned bit = direction < 0 ? 1u : 2u;
            auto it = controllers_.find(controller);
            if (it != controllers_.end())
            {
                auto& h = it->second;
                if (!eligible)
                {
                    h.cancelled = true;
                }
                if (h.held & bit)
                {
                    if (event == 1)
                    {
                        if (!h.chord && !h.cancelled && eligible)
                        {
                            zoom = direction;
                        }
                        h.held &= ~bit;
                        if (!h.held)
                        {
                            controllers_.erase(it);
                        }
                    }
                    return true;
                }
                if (event == 0 && eligible)
                {
                    h.held |= bit;
                    if (!h.chord && !h.cancelled)
                    {
                        h.chord = true;
                        toggle = true;
                    }
                    return true;
                }
            }
            if (event != 0 || !eligible)
            {
                return false;
            }
            controllers_[controller] = {bit, false, false};
            return true;
        }
    };

} // namespace kh3ap
