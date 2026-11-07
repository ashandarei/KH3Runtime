#pragma once

#include <cstdint>

namespace kh3ap
{
    // Shipping ETresChrUniqueID values used by playable-character features.
    // Keep distinct from ETresPlayerUniqueID / PlayerId, including at native
    // save-record boundaries that take one of each (Riku: character 2, player 8).
    enum class CharacterId : uint8_t
    {
        Unknown = 0,
        Sora = 1,
        Riku = 2,
        Kairi = 3,
        Aqua = 6,
        Roxas = 7,
        KingMickey = 10,
    };

    constexpr uint8_t character_id_value(CharacterId id)
    {
        return static_cast<uint8_t>(id);
    }
} // namespace kh3ap
