#pragma once

#include "kh3ap/features/player/player_ids.h"
#include <string_view>

namespace kh3ap
{
    constexpr bool roxas_chest_key_allowed(bool enabled, int player_id,
                                          std::wstring_view keyblade)
    {
        return enabled && player_id == player_id_value(PlayerId::Roxas) &&
               (keyblade == L"Oathkeeper" || keyblade == L"Oblivion");
    }
}
