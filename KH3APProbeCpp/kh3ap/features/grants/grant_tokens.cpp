#include "kh3ap/features/grants/grant_tokens.h"

#include <format>

namespace kh3ap
{
    auto RuntimeGrantTokens::classify(std::wstring_view runtime_token) -> RuntimeGrantTokenFamily
    {
        if (runtime_token.starts_with(L"ETresAbilityKind::"))
        {
            return RuntimeGrantTokenFamily::Ability;
        }
        if (runtime_token.starts_with(L"ETresVictoryBonusKind::"))
        {
            return RuntimeGrantTokenFamily::VictoryBonus;
        }
        if (!runtime_token.empty())
        {
            return RuntimeGrantTokenFamily::InventoryItem;
        }
        return RuntimeGrantTokenFamily::Unknown;
    }

    auto RuntimeGrantTokens::should_force_visible(std::wstring_view runtime_token) -> bool
    {
        // The native VBonus HUD call needs visibility enabled for victory-bonus
        // tokens; ability batches may still deliberately suppress presentation.
        return classify(runtime_token) == RuntimeGrantTokenFamily::VictoryBonus;
    }

    auto RuntimeGrantTokens::effective_show_info(const RuntimeItemGrantRequest& request) -> bool
    {
        return request.show_info || should_force_visible(request.runtime_token);
    }

    auto RuntimeGrantTokens::make_request_key(const RuntimeItemGrantRequest& request) -> std::wstring
    {
        return std::format(
                L"{}|{}|{}|{}",
                request.request_id.empty() ? std::wstring{L"<no-request-id>"} : request.request_id,
                request.index,
                request.runtime_token,
                request.grant_method);
    }

    auto RuntimeGrantTokens::make_batch_request_key(const RuntimeItemGrantBatchRequest& request) -> std::wstring
    {
        return std::format(
                L"{}|batch|{}",
                request.request_id.empty() ? std::wstring{L"<no-request-id>"} : request.request_id,
                request.grants.size());
    }

    auto RuntimeGrantTokens::build_apply_payload(const RuntimeItemGrantRequest& request) -> RuntimeItemApplyPayload
    {
        return RuntimeItemApplyPayload{
                .index = request.index,
                .item_name = request.item_name,
                .request_id = request.request_id,
                .action_kind = request.action_kind,
                .runtime_token = request.runtime_token,
                .grant_method = request.grant_method,
                .status = std::wstring{L"applied"},
                .error = std::wstring{},
        };
    }

    auto RuntimeGrantTokens::join(const std::vector<std::wstring>& values, std::wstring_view separator) -> std::wstring
    {
        std::wstring joined{};
        for (size_t index = 0; index < values.size(); ++index)
        {
            if (index > 0)
            {
                joined += separator;
            }
            joined += values[index];
        }
        return joined;
    }

    auto RuntimeGrantTokens::build_bundle_summary(
            const std::vector<std::wstring>& bundle_display,
            std::wstring_view randomized_summary,
            const std::vector<std::wstring>& randomized_tokens,
            std::wstring_view runtime_token) -> std::wstring
    {
        if (!bundle_display.empty())
        {
            return join(bundle_display, L", ");
        }
        if (!randomized_summary.empty())
        {
            return std::wstring{randomized_summary};
        }
        if (!randomized_tokens.empty())
        {
            return join(randomized_tokens, L", ");
        }
        if (!runtime_token.empty())
        {
            return std::wstring{runtime_token};
        }
        return std::wstring{};
    }
}
