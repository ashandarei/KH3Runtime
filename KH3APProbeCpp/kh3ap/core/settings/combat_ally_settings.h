#pragma once

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/player/player_ids.h"

namespace kh3ap
{
    constexpr bool valid_combat_ally_id(int64_t id)
    {
        return id == -1 || id == player_id_value(PlayerId::Aqua) ||
            id == player_id_value(PlayerId::Kairi) || id == player_id_value(PlayerId::Roxas) ||
            id == player_id_value(PlayerId::Riku);
    }

    inline int32_t read_combat_ally_id(const JsonObject& settings)
    {
        const auto id = settings.int64_field("combat_ally_id").value_or(-1);
        return valid_combat_ally_id(id) ? static_cast<int32_t>(id) : -1;
    }
}
