#pragma once

#include <cstdint>

namespace kh3ap
{
    // Shipping ETresPlayerUniqueID values. These are NOT ETresChrUniqueID
    // values (for example, Sora's character kind is 1, not his player ID 0).
    enum class PlayerId : uint8_t
    {
        Sora = 0,
        SoraKH1 = 7,
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

    // The contiguous SORA_KH3 through SORA_KH1 block. Special factory IDs
    // (tutorial, Hundred Acre Wood and others) are outside this range.
    constexpr bool is_standard_sora_player_id(int id)
    {
        return id >= player_id_value(PlayerId::Sora) && id <= player_id_value(PlayerId::SoraKH1);
    }
}
