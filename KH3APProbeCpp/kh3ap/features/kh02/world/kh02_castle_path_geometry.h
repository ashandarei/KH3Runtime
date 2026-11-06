#pragma once
#include <algorithm>
#include <array>

namespace kh3ap
{
    struct Kh02TriggerPlacement
    {
        std::array<float, 3> center{};
        std::array<float, 3> extent{};
        std::array<float, 3> rotation{};
    };

    namespace kh02_castle_path
    {
        constexpr int aerial_upper_reach_cm = 4'096;

        // Raise only the top of the road-spanning volume. Keep its lower edge,
        // horizontal crossing and rotation at their authored values.
        inline auto placement(const Kh02TriggerPlacement& authored, bool expanded) -> Kh02TriggerPlacement
        {
            auto adjusted = authored;
            if (expanded)
            {
                const float lift = std::max(0.f, (aerial_upper_reach_cm - authored.extent[2]) * .5f);
                adjusted.center[2] += lift;
                adjusted.extent[2] += lift;
            }
            return adjusted;
        }
    } // namespace kh02_castle_path
} // namespace kh3ap
