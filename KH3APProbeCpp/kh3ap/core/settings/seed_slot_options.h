#pragma once

#include "kh3ap/bridge/serialization/json_fields.h"
#include <string_view>

namespace kh3ap
{
    // YAML toggles are encoded as 0/1. Accept JSON booleans too, but never
    // search nested objects or treat an arbitrary nonzero integer as true.
    inline auto seed_bool_option(const JsonObject& slot, std::string_view key,
            bool fallback = false) -> bool
    {
        if (const auto value = slot.bool_field(key)) return *value;
        const auto value = slot.int64_field(key);
        if (value == 0) return false;
        if (value == 1) return true;
        return fallback;
    }

    inline auto seed_choice_option(const JsonObject& slot, std::string_view key,
            int32_t maximum, int32_t fallback = 0) -> int32_t
    {
        const auto value = slot.int64_field(key);
        return value && *value >= 0 && *value <= maximum
                ? static_cast<int32_t>(*value) : fallback;
    }

    inline auto seed_keyblade_levels_start_at_max(const JsonObject& slot) -> bool
    {
        return slot.int64_field("keyblade_levels") == 0 ||
                slot.string_field("keyblade_levels") == L"start_at_max";
    }
}
