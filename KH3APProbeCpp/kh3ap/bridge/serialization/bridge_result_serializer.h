#pragma once

#include "kh3ap/core/types.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeBridgeResultSerializer
    {
    public:
        static auto serialize_ap_message_result(
                const RuntimeApMessageRequest& request,
                std::wstring_view status,
                std::wstring_view error,
                std::wstring_view protocol_name,
                int64_t protocol_version) -> std::string;

        static auto serialize_manual_item_grant_result(
                const RuntimeItemGrantRequest& request,
                std::wstring_view status,
                std::wstring_view error,
                std::wstring_view protocol_name,
                int64_t protocol_version) -> std::string;
    };
}
