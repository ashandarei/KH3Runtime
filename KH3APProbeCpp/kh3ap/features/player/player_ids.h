#pragma once

#include <cstdint>

namespace kh3ap
{
    // Shipping ETresPlayerUniqueID values. These are NOT ETresChrUniqueID
    // values (for example, Sora's character kind is 1, not his player ID 0).
    enum class PlayerId : uint8_t
    {
        Sora = 0,
        Riku = 8,
        Aqua = 10,
        Kairi = 16,
        Roxas = 17,
        KingMickey = 18,
    };

    constexpr int player_id_value(PlayerId id)
    {
        return static_cast<int>(id);
    }
}
