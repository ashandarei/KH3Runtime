#pragma once

#include "kh3ap/core/types.h"

#include <optional>
#include <string>

namespace kh3ap
{
    class RuntimeRequestParser
    {
    public:
        static auto parse_ap_message(const std::string& request_text) -> std::optional<RuntimeApMessageRequest>;
        static auto parse_item_grant(const std::string& request_text) -> std::optional<RuntimeItemGrantRequest>;
        static auto parse_item_grant_batch(const std::string& request_text) -> std::optional<RuntimeItemGrantBatchRequest>;

    private:
        static auto is_supported_batch_action(const std::wstring& action_kind) -> bool;
    };
}
