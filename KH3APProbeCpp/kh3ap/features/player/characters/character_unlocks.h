#pragma once

#include "kh3ap/features/player/player_ids.h"
#include <atomic>
#include <cstdint>
#include <string_view>

namespace kh3ap::character_unlocks
{
    inline constexpr uint32_t aqua_unlock_mask = 1;
    inline constexpr uint32_t kairi_unlock_mask = 2;
    inline constexpr uint32_t roxas_unlock_mask = 4;
    inline constexpr uint32_t riku_unlock_mask = 8;

    constexpr auto bit(int player_id) -> uint32_t
    {
        switch (player_id)
        {
        case player_id_value(PlayerId::Riku):
            return riku_unlock_mask;
        case player_id_value(PlayerId::Aqua):
            return aqua_unlock_mask;
        case player_id_value(PlayerId::Kairi):
            return kairi_unlock_mask;
        case player_id_value(PlayerId::Roxas):
            return roxas_unlock_mask;
        default:
            return 0;
        }
    }

    constexpr auto token_bit(std::wstring_view token) -> uint32_t
    {
        if (token == L"KH3AP_CHARACTER_UNLOCK::8")
        {
            return bit(player_id_value(PlayerId::Riku));
        }
        if (token == L"KH3AP_CHARACTER_UNLOCK::10")
        {
            return bit(player_id_value(PlayerId::Aqua));
        }
        if (token == L"KH3AP_CHARACTER_UNLOCK::16")
        {
            return bit(player_id_value(PlayerId::Kairi));
        }
        if (token == L"KH3AP_CHARACTER_UNLOCK::17")
        {
            return bit(player_id_value(PlayerId::Roxas));
        }
        return 0;
    }

    // Rebuilt by the existing seed-bound receipt catalog event. The UI never
    // reads files, and ownership survives save reloads through AP receipt history.
    inline std::atomic<uint32_t> received_mask{};

    inline auto available(int player_id) -> bool
    {
        return player_id == -1 ||
               (received_mask.load(std::memory_order_acquire) & bit(player_id)) != 0;
    }
} // namespace kh3ap::character_unlocks
