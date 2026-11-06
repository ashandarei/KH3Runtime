#pragma once

#include "kh3ap/core/types.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeItemApplySerializer
    {
    public:
        static auto serialize_event(
                const RuntimeItemApplyPayload& payload,
                std::wstring_view protocol_name,
                int64_t protocol_version) -> std::string;

        static auto serialize_batch_event(
                const RuntimeItemApplyBatchPayload& payload,
                std::wstring_view protocol_name,
                int64_t protocol_version) -> std::string;
    };
}
