#pragma once
#include "kh3ap/core/types.h"
#include "item_delivery_limits.h"
#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <set>

namespace kh3ap::item_delivery
{
    // Values only: the queue never retains a gameplay UObject between frames.
    // A receipt's primary and secondary actions form one acknowledgement unit.
    class DeliveryQueue
    {
    public:
        static constexpr size_t capacity = limits::request_actions;
        static constexpr size_t simple_frame_limit = limits::simple_actions_per_frame;
        static constexpr size_t complex_frame_limit = limits::complex_actions_per_frame;
        uint64_t mailbox_revision{};

        auto replace(const RuntimeItemGrantBatchRequest& request) -> bool
        {
            if (request.grants.empty() || request.grants.size() > capacity) return false;
            std::set<int64_t> seen;
            size_t group_size = 0;
            for (size_t i = 0; i < request.grants.size(); ++i)
            {
                if (!i || request.grants[i].index != request.grants[i - 1].index)
                {
                    group_size = 0;
                    if (!seen.insert(request.grants[i].index).second) return false;
                }
                if (++group_size > limits::receipt_actions) return false;
            }
            if (source_.process_id != request.process_id || source_.seed_name != request.seed_name ||
                source_.slot != request.slot || source_.delivery_epoch != request.delivery_epoch || source_.action_kind != request.action_kind)
            { successes_.clear(); acknowledged_.clear(); }
            source_ = {.request_schema = request.request_schema, .process_id = request.process_id,
                .delivery_epoch = request.delivery_epoch, .seed_name = request.seed_name, .slot = request.slot, .request_id = request.request_id,
                .action_kind = request.action_kind, .reapply_context_key = request.reapply_context_key};
            groups_.clear();
            for (const auto& grant : request.grants)
            {
                if (groups_.empty() || groups_.back().front().index != grant.index)
                    groups_.emplace_back();
                groups_.back().push_back(grant);
            }
            // Retain successful secondary actions only while their receipt is
            // still pending, including when the client refreshes a request.
            std::map<std::wstring, RuntimeItemApplyPayload> retained;
            std::set<std::wstring> retained_acks;
            for (const auto& grant : request.grants)
            {
                if (const auto found = successes_.find(key(grant)); found != successes_.end())
                    retained.emplace(found->first, found->second);
                if (acknowledged_.contains(key(grant))) retained_acks.insert(key(grant));
            }
            successes_ = std::move(retained);
            acknowledged_ = std::move(retained_acks);
            // A worker snapshot can lag the game thread. Refreshing it must
            // not requeue receipt groups whose completion was already sent.
            std::erase_if(groups_, [&](const auto& group) {
                return std::all_of(group.begin(), group.end(), [&](const auto& grant) { return acknowledged_.contains(key(grant)); });
            });
            return true;
        }

        [[nodiscard]] auto empty() const -> bool { return groups_.empty(); }
        [[nodiscard]] auto cached(const RuntimeItemGrantRequest& grant) const -> std::optional<RuntimeItemApplyPayload>
        {
            const auto found = successes_.find(key(grant));
            return found == successes_.end() ? std::nullopt : std::optional{found->second};
        }

        auto select(bool restricted_context, const std::function<bool(std::span<const RuntimeItemGrantRequest>)>& context_allowed) const
                -> RuntimeItemGrantBatchRequest
        {
            auto batch = metadata();
            bool simple = true;
            for (const auto& group : groups_)
            {
                // Keep every receipt atomic, but let eligible later receipts pass
                // a reward that needs a different playable character/context.
                if (restricted_context && !context_allowed(group)) continue;
                const bool direct = std::any_of(group.begin(), group.end(), [](const auto& g) {
                    return g.grant_method == L"direct_player_bad_status" || g.grant_method == L"king_mickey_trap" || g.grant_method == L"betrayal_trap" || g.grant_method == L"bp_tres_start_game_over";
                });
                if (direct && !batch.grants.empty()) break;
                const bool group_simple = std::all_of(group.begin(), group.end(), [](const auto& g) {
                    return g.grant_method == L"bp_tres_get_item" && g.runtime_token.starts_with(L"ITEM_");
                });
                const size_t limit = simple && group_simple ? simple_frame_limit : complex_frame_limit;
                if (!batch.grants.empty() && batch.grants.size() + group.size() > limit) break;
                batch.grants.insert(batch.grants.end(), group.begin(), group.end());
                simple = simple && group_simple;
                if (direct || batch.grants.size() >= limit) break;
            }
            return batch;
        }

        auto remember(const RuntimeItemGrantRequest& grant, const RuntimeItemApplyPayload& payload) -> void
        {
            successes_[key(grant)] = payload;
        }
        auto complete_receipts(const RuntimeItemGrantBatchRequest& selected) const -> std::vector<RuntimeItemApplyPayload>
        {
            std::vector<RuntimeItemApplyPayload> complete;
            std::set<int64_t> selected_indices;
            for (const auto& grant : selected.grants) selected_indices.insert(grant.index);
            for (const auto& group : groups_)
            {
                if (!selected_indices.contains(group.front().index)) continue;
                if (std::all_of(group.begin(), group.end(), [&](const auto& g) { return cached(g).has_value(); }))
                    for (const auto& grant : group) complete.push_back(*cached(grant));
            }
            return complete;
        }

        auto acknowledge(const std::vector<RuntimeItemApplyPayload>& applied) -> void
        {
            std::set<int64_t> indices;
            for (const auto& payload : applied) indices.insert(payload.index);
            std::erase_if(groups_, [&](const auto& group) {
                if (!indices.contains(group.front().index)) return false;
                for (const auto& grant : group) acknowledged_.insert(key(grant));
                return true;
            });
        }
        auto clear() -> void { groups_.clear(); successes_.clear(); acknowledged_.clear(); }
        [[nodiscard]] auto completes_request(const std::vector<RuntimeItemApplyPayload>& applied) const -> bool
        {
            std::set<int64_t> indices;
            for (const auto& payload : applied) indices.insert(payload.index);
            return std::all_of(groups_.begin(), groups_.end(), [&](const auto& group) {
                return indices.contains(group.front().index);
            });
        }

    private:
        RuntimeItemGrantBatchRequest source_;
        std::vector<std::vector<RuntimeItemGrantRequest>> groups_;
        std::map<std::wstring, RuntimeItemApplyPayload> successes_;
        std::set<std::wstring> acknowledged_;
        static auto key(const RuntimeItemGrantRequest& g) -> std::wstring
        {
            // Negative indices are commands (e.g. DeathLink), not durable AP
            // receipt identities. A new command or explicit AP Replay request
            // must execute again even when its token/context is unchanged.
            const auto request_scope = g.index < 0 || g.action_kind == L"ap_replay_batch" ? g.request_id + L"|" : L"";
            return request_scope + std::to_wstring(g.index) + L"|" + g.reapply_context_key + L"|" + g.runtime_token + L"|" +
                    g.runtime_flag_name + L"|" + g.grant_method;
        }
        auto metadata() const -> RuntimeItemGrantBatchRequest
        {
            return {.request_schema = source_.request_schema, .process_id = source_.process_id,
                    .delivery_epoch = source_.delivery_epoch, .seed_name = source_.seed_name, .slot = source_.slot, .request_id = source_.request_id,
                    .action_kind = source_.action_kind, .reapply_context_key = source_.reapply_context_key};
        }
    };
}
