#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include "kh3ap/features/player/player_ids.h"

namespace kh3ap
{
    struct RuntimeTrapSpec
    {
        std::wstring_view token;
        std::wstring_view method;
        std::wstring_view name;
    };

    inline constexpr std::wstring_view king_mickey_trap_token = L"KH3AP_TRAP::KING_MICKEY";
    inline constexpr std::wstring_view king_mickey_trap_method = L"king_mickey_trap";
    inline constexpr std::array runtime_trap_specs{
        RuntimeTrapSpec{L"ETresBadStatusType::TRES_BADSTAT_SNEEZE", L"direct_player_bad_status", L"Sneeze Trap"},
        RuntimeTrapSpec{L"ETresBadStatusType::TRES_BADSTAT_BURN", L"direct_player_bad_status", L"Burn Trap"},
        RuntimeTrapSpec{king_mickey_trap_token, king_mickey_trap_method, L"King Mickey Trap"}};

    constexpr auto find_runtime_trap_spec(std::wstring_view token, std::wstring_view method)
        -> const RuntimeTrapSpec*
    {
        for (const auto& spec : runtime_trap_specs)
        {
            if (spec.token == token && spec.method == method)
            {
                return &spec;
            }
        }
        return nullptr;
    }

    constexpr bool supports_alternate_bad_status(std::wstring_view token, std::wstring_view method)
    {
        return method == L"direct_player_bad_status" && find_runtime_trap_spec(token, method);
    }

    constexpr bool alternate_bad_status_player_allowed(int player)
    {
        return player == player_id_value(PlayerId::Aqua) ||
               player == player_id_value(PlayerId::Kairi) ||
               player == player_id_value(PlayerId::Roxas);
    }

    // Match the existing trap allowlist. In particular, Hopping/athletic steps
    // (9), ShootFlow (41), and AthleticFlow/airstepping (42) must defer traps.
    constexpr bool runtime_trap_state_allowed(uint8_t state)
    {
        for (const auto allowed : {1, 3, 12, 15, 16, 18, 81, 86, 89})
        {
            if (state == allowed)
            {
                return true;
            }
        }
        return false;
    }
}
