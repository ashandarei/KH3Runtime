#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once
#include <utility>

#include "kh3ap/bridge/session/client_presentation.h"
#include "kh3ap/bridge/serialization/json.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace kh3ap::receipt_accounting
{
    inline constexpr auto recent_receipt_count = 5;

    inline constexpr int64_t maximum_receipt_index = 1000000;

    using Json = kh3ap::json::Value;
    inline auto field(const Json& value, const std::string& key) -> const Json&
    {
        static const Json missing;
        if (!value.is_object())
        {
            return missing;
        }
        const auto found = value.get_ref<const Json::object_t&>().find(key);
        return found == value.get_ref<const Json::object_t&>().end() ? missing : found->second;
    }
    inline auto text(const Json& value) -> std::string
    {
        return value.is_string() ? value.get_ref<const std::string&>() : std::string{};
    }
    inline auto integer(const Json& value) -> int64_t
    {
        if (!value.is_number() || !std::isfinite(value.get<double>()) ||
            std::abs(value.get<double>()) > static_cast<double>(kh3ap::json::max_safe_integer) ||
            std::trunc(value.get<double>()) != value.get<double>())
        {
            throw std::runtime_error("invalid_native_receipt_integer");
        }
        return static_cast<int64_t>(value.get<double>());
    }
    struct Applied
    {
        std::string first_at, last_at, request, action, source;
    };

    // The reconciliation worker owns this model. Acknowledged AP history and
    // currently loaded save receipts are deliberately separate: loading an old
    // save must replay its missing grants even when AP acknowledged them before.
    class Accounting
    {
        std::map<int64_t, Applied> applied;
        Json last_ack;

    public:
        auto checkpoint() const -> Json
        {
            Json::array_t entries;
            for (const auto& [index, item] : applied)
            {
                entries.emplace_back(Json{{"index", index},
                                          {"first_at", item.first_at},
                                          {"last_at", item.last_at},
                                          {"request", item.request},
                                          {"action", item.action},
                                          {"source", item.source}});
            }
            return Json{{"version", 1}, {"applied", std::move(entries)}, {"last_ack", last_ack}};
        }
        auto restore(const Json& value) -> void
        {
            if (!value.is_object() ||
                !value.get_ref<const Json::object_t&>().contains("last_ack") ||
                integer(field(value, "version")) != 1 || !field(value, "applied").is_array() ||
                (!field(value, "last_ack").is_null() && !field(value, "last_ack").is_object()))
            {
                throw std::runtime_error("invalid_native_receipt_accounting");
            }
            std::map<int64_t, Applied> next;
            for (const auto& row : value["applied"].get_ref<const Json::array_t&>())
            {
                const auto index = integer(field(row, "index"));
                Applied entry{text(field(row, "first_at")), text(field(row, "last_at")),
                              text(field(row, "request")), text(field(row, "action")),
                              text(field(row, "source"))};
                if (index < 0 || index > maximum_receipt_index || entry.first_at.empty() ||
                    entry.last_at.empty() || !next.emplace(index, std::move(entry)).second)
                {
                    throw std::runtime_error("invalid_native_receipt_accounting_entry");
                }
            }
            applied = std::move(next);
            last_ack = value["last_ack"];
        }
        auto remember(const std::set<int64_t>& active, const std::string& at) -> bool
        {
            bool changed{};
            for (const auto index : active)
            {
                changed |=
                    applied.emplace(index, Applied{at, at, {}, {}, "native_receipt_membership"})
                        .second;
            }
            return changed;
        }
        auto complete(const Json& raw, const std::set<int64_t>& active,
                      const Json::object_t& descriptions, int64_t pending_count) -> Json
        {
            Json event = raw;
            Json::array_t indices, actions;
            std::set<int64_t> first_ack_indices;
            const auto status = text(field(raw, "status"));
            if (field(raw, "applied_indices").is_array())
            {
                indices = raw["applied_indices"].get_ref<const Json::array_t&>();
            }
            else if (field(raw, "index").is_number() && integer(raw["index"]) >= 0)
            {
                indices.emplace_back(raw["index"]);
            }
            std::set<int64_t> unique;
            Json::array_t receipt_indices;
            for (const auto& value : indices)
            {
                const auto index = integer(value);
                if (index < 0)
                {
                    continue; // Commands such as DeathLink are not AP receipts.
                }
                if (index > maximum_receipt_index)
                {
                    throw std::runtime_error("invalid_native_ack_indices");
                }
                if (!unique.insert(index).second)
                {
                    continue; // Primary/secondary actions share one receipt.
                }
                receipt_indices.emplace_back(index);
                if (status != "applied" && status != "partial")
                {
                    continue;
                }
                if (!active.contains(index) || !applied.contains(index))
                {
                    throw std::runtime_error("native_ack_before_receipt_commit");
                }
                auto& entry = applied.at(index);
                const auto first_ack = entry.request.empty();
                const auto at = text(field(raw, "applied_at"));
                if (!at.empty())
                {
                    entry.last_at = at;
                }
                entry.request = text(field(raw, "request_id"));
                entry.action = text(field(raw, "action_kind"));
                entry.source = "runtime_item_apply_event";
                const auto description = descriptions.find(std::to_string(index));
                if (description != descriptions.end())
                {
                    actions.emplace_back(description->second);
                    if (first_ack)
                    {
                        first_ack_indices.insert(index);
                    }
                }
            }
            event["schema"] = "kh3ap.receipt_ack.v1";
            event["receipt_bookkeeping_owner"] = "dll";
            event["indices"] = receipt_indices;
            event["newly_applied_indices"] = (status == "applied" || status == "partial")
                                                 ? Json(receipt_indices)
                                                 : Json(Json::array_t{});
            event["client_notifications"] =
                client_presentation::applied_notifications(actions, first_ack_indices);
            event["applied_actions"] = std::move(actions);
            event["pending_item_count"] = pending_count;
            event["retired_runtime_grant_files"] = Json::array_t{};
            std::string encoded_indices;
            kh3ap::json::write(Json(receipt_indices), encoded_indices);
            event["event_key"] = text(field(raw, "request_id")) + "|" + status + "|" +
                                 text(field(raw, "applied_at")) + "|" + encoded_indices;
            last_ack = event;
            return event;
        }
        auto acknowledgments() const -> const Json&
        {
            return last_ack;
        }

        auto project(Json identity, const Json::object_t& descriptions,
                     const std::map<int64_t, Json>& facts,
                     const std::map<int64_t, std::string>& policies,
                     const std::set<int64_t>& active, const std::vector<int64_t>& pending,
                     const Json& delivery_summary, const Json& repairs, const Json& forced,
                     bool ready, const std::string& reason) const -> std::map<std::string, Json>
        {
            identity["protocol_name"] = "kh3_ap_file_bridge";
            identity["protocol_version"] = 1;
            const auto document = [&](const std::string& schema)
            {
                auto value = identity;
                value["schema"] = schema;
                return value;
            };
            Json::object_t entries, status_counts, policy_counts;
            Json::array_t ledger_applied, ledger_pending, active_indices, pending_actions,
                grantable;
            std::vector<Json> latest;
            int64_t ledger_last = -1, active_last = -1;
            for (const auto& [index, fact] : facts)
            {
                const auto description = descriptions.find(std::to_string(index));
                Json entry = description == descriptions.end() ? fact : description->second;
                entry["index"] = index;
                const auto policy = policies.at(index);
                entry["replay_policy"] = policy;
                const auto marked = applied.find(index);
                const auto status = marked == applied.end() ? "pending" : "applied";
                entry["status"] = status;
                auto& statuses = status_counts[status];
                statuses = statuses.is_number_integer() ? statuses.get<int64_t>() + 1 : 1;
                auto& policies_count = policy_counts[policy];
                policies_count =
                    policies_count.is_number_integer() ? policies_count.get<int64_t>() + 1 : 1;
                if (marked == applied.end())
                {
                    ledger_pending.emplace_back(index);
                }
                else
                {
                    const auto& mark = marked->second;
                    entry["applied_at_utc"] = mark.first_at;
                    entry["last_marked_applied_at_utc"] = mark.last_at;
                    entry["last_request_id"] = mark.request;
                    entry["last_action_kind"] = mark.action;
                    entry["apply_source"] = mark.source;
                    ledger_applied.emplace_back(index);
                    if (index == ledger_last + 1)
                    {
                        ledger_last = index;
                    }
                    latest.emplace_back(entry);
                }
                if (active.contains(index))
                {
                    active_indices.emplace_back(index);
                    if (index == active_last + 1)
                    {
                        active_last = index;
                    }
                }
                entries[std::to_string(index)] = std::move(entry);
            }
            for (const auto index : pending)
            {
                const auto description = descriptions.find(std::to_string(index));
                if (description == descriptions.end())
                {
                    throw std::runtime_error("native_pending_description_missing");
                }
                pending_actions.emplace_back(description->second);
                if (field(description->second, "native_supported").is_boolean() &&
                    description->second["native_supported"].get<bool>())
                {
                    grantable.emplace_back(description->second);
                }
            }
            std::sort(latest.begin(), latest.end(),
                      [](const Json& a, const Json& b)
                      {
                          const auto left = text(field(a, "last_marked_applied_at_utc")),
                                     right = text(field(b, "last_marked_applied_at_utc"));
                          return left != right ? left > right
                                               : integer(a["index"]) > integer(b["index"]);
                      });
            if (latest.size() > recent_receipt_count)
            {
                latest.resize(recent_receipt_count);
            }
            auto ledger = document("kh3ap.receipt_ledger.v1");
            ledger["items"] = std::move(entries);
            auto summary = document("kh3ap.receipt_ledger_summary.v1");
            summary["source"] = "item_delivery_ledger.json";
            summary["total_count"] = facts.size();
            summary["pending_count"] = ledger_pending.size();
            summary["applied_count"] = ledger_applied.size();
            summary["pending_indices"] = std::move(ledger_pending);
            summary["applied_indices"] = ledger_applied;
            summary["last_applied_index"] = ledger_last;
            summary["status_counts"] = std::move(status_counts);
            summary["replay_policy_counts"] = std::move(policy_counts);
            summary["latest_applied"] = std::move(latest);
            auto state = document("kh3ap.receipt_state.v1");
            state["applied_item_indices"] = std::move(ledger_applied);
            state["last_applied_index"] = ledger_last;
            state["pending_regrant_bridge_keys"] = repairs;
            state["server_forced_local_item_indices"] = forced;
            state["last_applied_actions"] = field(last_ack, "applied_actions").is_array()
                                                ? last_ack["applied_actions"]
                                                : Json(Json::array_t{});
            auto delivery = document("kh3ap.delivery_snapshot.v1");
            delivery["total_received_item_count"] = facts.size();
            delivery["applied_item_indices"] = std::move(active_indices);
            delivery["last_applied_index"] = active_last;
            delivery["pending_items"] = pending_actions;
            delivery["native_delivery_summary"] = delivery_summary;
            delivery["native_delivery_epoch"] = identity["delivery_epoch"];
            delivery["native_delivery_ready"] = ready;
            delivery["native_delivery_reason"] = reason;
            auto consumer = document("kh3ap.consumer_snapshot.v1");
            consumer["last_applied_index"] = active_last;
            consumer["pending_action_count"] = pending_actions.size();
            consumer["grantable_action_count"] = grantable.size();
            consumer["next_native_grant"] = grantable.empty() ? Json{} : grantable.front();
            consumer["pending_actions"] = std::move(pending_actions);
            auto apply_state = document("kh3ap.receipt_apply_state.v1");
            apply_state["last_processed_event_key"] = field(last_ack, "event_key");
            apply_state["last_processed_event"] = last_ack;
            auto grant_state = document("kh3ap.receipt_grant_state.v1");
            grant_state["status"] = ready && pending.empty() ? "applied" : "pending";
            grant_state["pending_item_count"] = pending.size();
            grant_state["applied_indices"] = field(last_ack, "indices");
            return {{"item_delivery_ledger.json", std::move(ledger)},
                    {channels::item_delivery_ledger_summary, std::move(summary)},
                    {channels::item_delivery_state, std::move(state)},
                    {channels::item_delivery, std::move(delivery)},
                    {channels::item_consumer_plan, std::move(consumer)},
                    {channels::runtime_item_apply_state, std::move(apply_state)},
                    {channels::runtime_item_grant_state, std::move(grant_state)}};
        }
    };
} // namespace kh3ap::receipt_accounting
