#include "kh3ap/features/grants/trap_catalog.h"
#pragma once
#include "kh3ap/core/types.h"
#include "item_delivery_limits.h"
#include "kh3ap/features/grants/keyblades/keyblade_level_limits.h"
#include "kh3ap/bridge/rewards/receipts.h"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace kh3ap::item_delivery
{
    // Declarative AP metadata only. Expansion uses the entire sorted receipt
    // history, including saved and naturally delivered receipts.
    struct RewardTemplate
    {
        RuntimeItemGrantRequest grant;
        std::vector<std::wstring> flag_rows;
        std::wstring progressive_base;
        int64_t shard_required{7};
    };
    struct ReceiptTemplate
    {
        RewardTemplate primary;
        std::vector<RewardTemplate> rewards;
        bool supported{}, local{}, level_up{}, always_on{}, bridge_required{}, inventory{};
        std::string bridge_key;
    };
    using ExpandedCatalog = std::map<int64_t, std::vector<RuntimeItemGrantRequest>>;

    inline auto durable_repair_method(std::wstring_view method) -> bool
    {
        return method == L"controller_unlock" || method == L"durable_sora_ability" ||
            method == L"durable_sora_vbonus" || method == L"progressive_keyblade_level" || method == L"keyblade_style";
    }
    // Inventory items and durable ability sources survive the in-memory Retry
    // path. Advance their backup membership with the mutation ACK, even after
    // CreateRetryPoint. VBonuses are deliberately excluded: room initialization
    // restores their checkpoint counts AFTER the generic save-data copy, even
    // though their grant temporarily updates both banks. They must replay.
    inline auto writes_inventory_backup(std::wstring_view method) -> bool
    {
        return method==L"durable_sora_ability" || method==L"bp_tres_get_item" || method==L"bp_tres_get_item_add_munny" ||
            method==L"controller_unlock" || method==L"keyblade_style";
    }
    inline auto repairable(const ReceiptTemplate& item) -> bool
    {
        return item.local && item.supported && !item.level_up && !item.always_on && !item.inventory &&
            (item.bridge_key.starts_with("Event:") || item.bridge_key.starts_with("VBonus:")) &&
            durable_repair_method(item.primary.grant.grant_method);
    }

    inline auto expand_catalog(const std::map<int64_t, ReceiptTemplate>& receipts) -> ExpandedCatalog
    {
        ExpandedCatalog result;
        std::map<std::wstring, int64_t> tokens, progressive;
        int64_t shards{};
        for (const auto& [index, receipt] : receipts)
        {
            auto& grants = result[index];
            const auto expand = [&](const RewardTemplate& reward) {
                auto grant = reward.grant;
                grant.index = index;
                const auto token = grant.runtime_token;
                if (!reward.progressive_base.empty())
                    grant.runtime_token = reward.progressive_base + L"::" +
                        std::to_wstring(std::min(++progressive[reward.progressive_base], int64_t{keyblade_levels::maximum}));
                else if (token == L"KH3AP_ULTIMA_WEAPON_SHARD")
                    grant.runtime_token = token + L"::" + std::to_wstring(++shards) + L"::" +
                        std::to_wstring(reward.shard_required);
                else if (!reward.flag_rows.empty())
                    grant.runtime_flag_name = reward.flag_rows[tokens[token] % reward.flag_rows.size()];
                ++tokens[token];
                grants.push_back(std::move(grant));
            };
            if (!receipt.supported) continue;
            expand(receipt.primary);
            // Level-up checks have one AP-owned reward, regardless of route metadata.
            if (!receipt.level_up) for (const auto& reward : receipt.rewards) expand(reward);
            if (grants.size() > limits::receipt_actions) throw std::runtime_error("receipt_bundle_capacity");
        }
        return result;
    }

    inline auto delivery_eligible(const ReceiptTemplate& item, int64_t index,
            const std::set<int64_t>& forced, const std::set<std::string>& repairs) -> bool
    {
        if (!item.supported) return false;
        if (!item.local || item.level_up) return true;
        if (item.always_on) return false;
        if (item.bridge_required || forced.contains(index)) return true;
        return repairable(item) && repairs.contains(item.bridge_key);
    }

    struct ScheduledDelivery
    {
        std::vector<int64_t> pending;
        std::vector<RuntimeItemGrantRequest> grants, traps;
    };
    inline auto schedule_catalog(const std::map<int64_t, ReceiptTemplate>& receipts,
            const ExpandedCatalog& expanded, const reconciliation::Receipts& active,
            const std::set<int64_t>& forced, const std::set<std::string>& repairs,
            const std::set<int64_t>& retry_preserved = {}) -> ScheduledDelivery
    {
        ScheduledDelivery result;
        for (const auto& [index, item] : receipts)
        {
            if (active.contains(index) || !delivery_eligible(item, index, forced, repairs)) continue;
            result.pending.push_back(index);
            auto group = expanded.at(index);
            // A mixed receipt may contain a retained ability/inventory item
            // and a rolled-back VBonus. Replay only its missing components.
            if (retry_preserved.contains(index))
                std::erase_if(group, [](const auto& grant) { return writes_inventory_backup(grant.grant_method); });
            // Reobserving a natural Event/VBonus must never duplicate inventory
            // secondaries. Ordinary AP delivery still owns the complete bundle.
            if (repairs.contains(item.bridge_key))
                std::erase_if(group, [](const auto& grant) { return !durable_repair_method(grant.grant_method); });
            const bool trap = group.size() == 1 && find_runtime_trap_spec(group.front().runtime_token, group.front().grant_method);
            auto& lane = trap ? result.traps : result.grants;
            if (lane.size() + group.size() <= limits::request_actions) lane.insert(lane.end(), group.begin(), group.end());
        }
        return result;
    }

    inline auto expand_command(const RuntimeItemGrantBatchRequest& command, const ExpandedCatalog& catalog)
        -> std::optional<RuntimeItemGrantBatchRequest>
    {
        if (!command.expand_catalog) return command;
        auto expanded = command;
        expanded.grants.clear(); expanded.expand_catalog = false;
        std::set<int64_t> seen;
        for (const auto& requested : command.grants)
        {
            if (!seen.insert(requested.index).second) continue;
            const auto found = catalog.find(requested.index);
            if (found == catalog.end() || found->second.empty()) return {};
            if (expanded.grants.size() + found->second.size() > limits::request_actions) return {};
            for (auto grant : found->second)
            {
                grant.request_id = command.request_id; grant.action_kind = command.action_kind;
                grant.reapply_context_key = command.reapply_context_key;
                grant.show_info = requested.show_info;
                expanded.grants.push_back(std::move(grant));
            }
        }
        return expanded;
    }
}
