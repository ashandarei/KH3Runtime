#pragma once

#include "kh3ap/core/types.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    enum class RuntimeGrantTokenFamily
    {
        Unknown,
        InventoryItem,
        Ability,
        VictoryBonus,
    };

    class RuntimeGrantTokens
    {
    public:
        static auto classify(std::wstring_view runtime_token) -> RuntimeGrantTokenFamily;
        static auto should_force_visible(std::wstring_view runtime_token) -> bool;
        static auto effective_show_info(const RuntimeItemGrantRequest& request) -> bool;
        static auto make_request_key(const RuntimeItemGrantRequest& request) -> std::wstring;
        static auto make_batch_request_key(const RuntimeItemGrantBatchRequest& request) -> std::wstring;
        static auto build_apply_payload(const RuntimeItemGrantRequest& request) -> RuntimeItemApplyPayload;
        static auto join(const std::vector<std::wstring>& values, std::wstring_view separator) -> std::wstring;
        static auto build_bundle_summary(
                const std::vector<std::wstring>& bundle_display,
                std::wstring_view randomized_summary,
                const std::vector<std::wstring>& randomized_tokens,
                std::wstring_view runtime_token) -> std::wstring;
    };
}
