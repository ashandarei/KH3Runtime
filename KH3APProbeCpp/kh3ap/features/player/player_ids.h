#pragma once

#include <cstdint>

namespace kh3ap
{
    // Shipping ETresPlayerUniqueID values. These are NOT ETresChrUniqueID
    // values (for example, Sora's character kind is 1, not his player ID 0).
    enum class PlayerId : uint8_t
    {
        Sora = 0,
        SoraCaribbean = 1,
        SoraMonstropolis = 2,
        SoraToyBox = 3,
        SoraKH3BX = 4,
        SoraKH3EW = 5,
        SoraKH2 = 6,
        SoraKH1 = 7,
        Riku = 8,
        RikuKH29 = 9,
        Aqua = 10,
        SoraTutorial = 11,
        SoraToyBoxGame = 12,
        SoraHundredAcreWood = 13,
        Unknown = 14,
        SoraYozora = 15,
        Kairi = 16,
        Roxas = 17,
        KingMickey = 18,
        SoraKH3SoKc = 19,
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
