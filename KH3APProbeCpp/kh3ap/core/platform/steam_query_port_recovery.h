#pragma once

#include <cstdint>
#include <optional>

namespace kh3ap
{
    constexpr uint32_t s_kh3_steam_app_id = 2552450;

    struct RuntimeSteamQueryPortDecision
    {
        uint16_t effective_port{0};
        bool replaced{false};
    };

    // Keep KH3's requested Steam query port whenever it is usable. Recovery is
    // deliberately limited to the exact supported Steam executable, a
    // successful client initialization, and KH3's own Steam app id.
    constexpr auto decide_runtime_steam_query_port(
            bool supported_steam_image,
            bool steam_client_initialized,
            uint32_t app_id,
            uint16_t requested_port,
            uint16_t game_port,
            bool requested_port_available,
            std::optional<uint16_t> available_candidate) noexcept
            -> RuntimeSteamQueryPortDecision
    {
        if (!supported_steam_image || !steam_client_initialized ||
            app_id != s_kh3_steam_app_id || requested_port == 0 ||
            requested_port_available || !available_candidate.has_value() ||
            *available_candidate == 0 ||
            *available_candidate == requested_port ||
            *available_candidate == game_port)
        {
            return {requested_port, false};
        }
        return {*available_candidate, true};
    }
}
