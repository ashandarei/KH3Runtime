#pragma once
#include "kh3ap/bridge/rewards/receipts.h"
#include "kh3ap/bridge/serialization/json.h"
#include <map>
#include <set>
#include <string>
#include <utility>

namespace kh3ap::client_presentation
{
    using Json = kh3ap::json::Value;
    inline auto field(const Json& value, const std::string& key) -> const Json&
    {
        static const Json missing;
        if (!value.is_object()) return missing;
        const auto found = value.get_ref<const Json::object_t&>().find(key);
        return found == value.get_ref<const Json::object_t&>().end() ? missing : found->second;
    }
    inline auto text(const Json& value) -> std::string
    { return value.is_string() ? value.get_ref<const std::string&>() : std::string{}; }

    inline auto replay_items(const reconciliation::Receipts& saved,
            const std::set<int64_t>& eligible, const Json::object_t& descriptions) -> Json
    {
        Json::array_t pending;
        for (const auto index : eligible)
        {
            if (saved.contains(index)) continue;
            // A recovered journal can be ahead of the server's last save.
            // Keep those receipts, but wait for their metadata before previewing them.
            const auto description = descriptions.find(std::to_string(index));
            if (description == descriptions.end()) continue;
            const auto& item = description->second;
            // Starting inventory and server commands are not AP replay previews.
            if (field(item, "location_id").is_number() && item["location_id"].get<double>() == -2) continue;
            if (field(item, "from_player").is_number() && item["from_player"].get<double>() == 0 &&
                text(field(item, "location_name")) == "Server") continue;
            pending.emplace_back(Json{{"index", index}, {"item_name", item["item_name"]}});
        }
        return pending;
    }

    // Native descriptions already contain resolved item names and grant kinds.
    // Preserve every batch's messages in the event transport, rather than having
    // the client infer notices from whichever receipt snapshot it last polled.
    inline auto applied_notifications(const Json::array_t& actions, const std::set<int64_t>& first_ack) -> Json
    {
        Json::array_t notices;
        std::set<std::pair<std::string, std::string>> seen;
        for (const auto& item : actions)
        {
            const auto name = text(field(item, "item_name"));
            if (name.empty()) continue;
            auto location = text(field(item, "location_name"));
            if (location.empty()) location = "?";
            if (location.starts_with("Always On - ")) location = "Starting Inventory";
            const auto append = [&](std::string event, std::string message) {
                if (!seen.emplace(event, message).second) return;
                Json notice{{"event", event}, {"message", message}, {"status", "applied"},
                    {"index", item["index"]}, {"item_name", name}, {"location_name", location},
                    {"bridge_key", field(item, "bridge_key")}};
                notices.emplace_back(std::move(notice));
            };
            append("item_applied", "KH3 item applied: " + name + " (" + location + ")");
            const auto& extras = field(item, "kh3_secondary_rewards");
            if (extras.is_array() && !extras.get_ref<const Json::array_t&>().empty())
            {
                std::string joined;
                for (const auto& extra : extras.get_ref<const Json::array_t&>())
                {
                    const auto reward = text(extra);
                    if (reward.empty()) continue;
                    if (!joined.empty()) joined += ", ";
                    joined += reward;
                }
                if (!joined.empty()) append("bundle_extras", "Bundle extras: " + name + " (" + location + ") [+ " + joined + "]");
            }
            const auto& action = field(item, "action");
            const auto kind = text(field(action, "kind"));
            if (kind == "grant_keyblade") append("keyblade_received", "Keyblade received: " + name);
            if (kind == "grant_controller_unlock" && first_ack.contains(static_cast<int64_t>(item["index"].get<double>())))
            {
                const auto unlock = text(field(action, "unlock_display_name"));
                if (!unlock.empty()) append("controller_unlock", unlock + " unlocked");
            }
        }
        return notices;
    }
}
