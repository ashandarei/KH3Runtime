#pragma once

#include "kh3ap/core/types.h"

#include <string>
#include <cstdint>

namespace kh3ap
{
    class RuntimePayloadSerializer
    {
    public:
        static auto serialize_context(const RuntimeContextSnapshot& snapshot, uint32_t process_id = 0) -> std::string;
        static auto serialize_loot_payload(const RuntimeLootPayload& payload) -> std::string;
    };
}
