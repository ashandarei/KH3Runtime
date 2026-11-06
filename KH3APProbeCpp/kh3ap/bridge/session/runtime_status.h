#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once
#include "kh3ap/features/grants/delivery/map_ap_grant_catalog.h"
#include "kh3ap/bridge/serialization/json.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap::runtime_status
{
    using Json = kh3ap::json::Value;
    inline auto field(const Json& j, std::string_view key) -> const Json&
    { static const Json empty; return j.contains(key) ? j[key] : empty; }
    inline auto text(const Json& j) -> std::string
    { return j.is_string() ? j.get_ref<const std::string&>() : std::string{}; }
    inline auto number(const Json& j) -> std::string
    { return j.is_number() ? std::to_string(static_cast<int64_t>(j.get<double>())) : text(j); }
    inline auto truth(const Json& j) -> std::string
    { return j.is_boolean() ? j.get<bool>() ? "true" : "false" : "-"; }
    inline auto path(std::string_view raw) -> std::string
    {
        const auto start = raw.find("/Game/");
        if (start == raw.npos) return {};
        raw.remove_prefix(start);
        return std::string(raw.substr(0, raw.find_first_of(". :|,\r\n\t")));
    }
    inline auto map_info(std::string_view raw) -> Json
    {
        const auto asset = path(raw);
        if (asset.empty()) return Json::object_t{};
        std::vector<std::string> parts;
        std::string_view rest{asset};
        while (!rest.empty())
        {
            const auto end = rest.find('/');
            if (end) parts.emplace_back(rest.substr(0, end));
            if (end == rest.npos) break;
            rest.remove_prefix(end + 1);
        }
        if (parts.size() < 3 || parts[0] != "Game" || (parts[1] != "Levels" && parts[1] != "Maps"))
            return Json::object_t{};
        if (parts[1] == "Levels" && parts.size() < 5) return Json::object_t{};
        const auto level = parts[1] == "Levels" && parts.size() >= 4 ? parts[3] : parts[2];
        auto canonical = asset;
        auto room = parts.back();
        if (parts.size() >= 6 && parts[1] == "Levels" && parts[4] == "umap" &&
            (room == level + "_gameflow" || room.starts_with(level + "_camera_")))
        { canonical = "/Game/Levels/" + parts[2] + "/" + level + "/" + level; room = level; }
        auto object_path = path(raw);
        auto object_name = room;
        const auto offset = raw.find(object_path);
        if (offset != raw.npos && offset + object_path.size() < raw.size() && raw[offset + object_path.size()] == '.')
        {
            const auto suffix = raw.substr(offset + object_path.size() + 1);
            object_name = std::string(suffix.substr(0, suffix.find_first_of(": |,\r\n\t")));
            object_path += "." + object_name;
        }
        if (canonical != asset) { object_path = canonical + "." + room; object_name = room; }
        return Json{{"asset_path", canonical}, {"object_path", object_path}, {"object_name", object_name},
            {"path_kind", parts[1]}, {"world_code", parts[2]}, {"level_id", level}, {"room_id", room}};
    }
    inline auto context_map(const Json& j) -> Json
    {
        auto result = map_info(text(field(j, "world")));
        if (!result.contains("asset_path")) result = map_info(text(field(j, "outer")));
        return result;
    }
    inline auto object_summary(const Json& j) -> std::string
    {
        const auto object = text(field(j, "object"));
        const auto source = text(field(j, "pawn_source"));
        auto label = object.empty() ? std::string{} : object.substr(0, object.find(" /"));
        const auto address = object.find('@');
        if (address != object.npos) label += " " + object.substr(address, object.find_first_not_of("0123456789", address + 1) - address);
        if (!source.empty()) return label.empty() ? source : label + " via " + source;
        return label.empty() ? "-" : label;
    }

    // Value-only worker projection. Engine state comes from native producers;
    // no diagnostic object properties or historical client files supply state.
    class Snapshot
    {
        Json context, map_ready, sora, ui, minimap, readiness;
    public:
        auto clear() -> void { context = nullptr; map_ready = nullptr; sora = nullptr; ui = nullptr; minimap = nullptr; readiness = nullptr; }
        static auto channel(std::string_view name) -> bool
        { return name == channels::runtime_context || name == channels::runtime_map_ready ||
            name == channels::runtime_sora_state || name == channels::runtime_blocking_ui_state ||
            name == channels::runtime_minimap_context || name == channels::runtime_item_grant_readiness; }
        auto observe(std::string_view name, const Json& value) -> void
        {
            if (name == channels::runtime_context)
            {
                const auto old_map = text(field(context_map(context), "asset_path"));
                const auto new_map = text(field(context_map(value), "asset_path"));
                if (old_map != new_map || text(field(context, "object")) != text(field(value, "object")))
                { sora = nullptr; minimap = nullptr; readiness = nullptr; }
                if (old_map != new_map) map_ready = nullptr;
                context = value;
            }
            else if (name == channels::runtime_map_ready)
            {
                if (text(field(context_map(context), "asset_path")) != text(field(context_map(value), "asset_path")))
                { context = nullptr; sora = nullptr; minimap = nullptr; readiness = nullptr; }
                map_ready = value;
            }
            else if (name == channels::runtime_sora_state) sora = value;
            else if (name == channels::runtime_blocking_ui_state) ui = value;
            else if (name == channels::runtime_minimap_context) minimap = value;
            else if (name == channels::runtime_item_grant_readiness) readiness = value;
        }
        auto project(uint32_t pid, std::string_view seed, int64_t slot, int64_t epoch, bool ready) const -> Json
        {
            auto map = context_map(context);
            const auto& source = map.contains("asset_path") ? context : map_ready;
            if (!map.contains("asset_path")) map = context_map(map_ready);
            auto origin = text(field(source, "pawn_source"));
            if (origin.empty()) origin = text(field(source, "source"));
            if (origin.empty()) origin = text(field(source, "hook"));
            const auto asset = text(field(map, "asset_path"));
            const auto wide_asset = std::wstring(asset.begin(), asset.end());
            const auto* rule = find_runtime_map_ap_grant_rule(wide_asset);
            const auto safety = asset.empty() ? "-" : rule ? rule->safety == RuntimeMapApGrantSafety::Safe ? "yes" : "no" : "unknown";
            auto map_label = asset.empty() ? "-" : text(field(map, "world_code")) + "/" + text(field(map, "level_id")) + "/" + text(field(map, "room_id"));
            if (!asset.empty() && !origin.empty()) map_label += " (" + origin + ")";
            auto sora_label = std::string{"-"};
            if (text(field(sora, "status")) == "unavailable") sora_label = "Unavailable (" + text(field(sora, "reason")) + ")";
            else if (!text(field(sora, "state_class")).empty())
            {
                sora_label = text(field(sora, "state_class"));
                auto details = number(field(sora, "current_state_id"));
                if (!details.empty()) details = "id=" + details;
                for (const auto key : {"source", "observed_at_utc"})
                {
                    const auto value = text(field(sora, key));
                    if (!value.empty()) { if (!details.empty()) details += ", "; details += value; }
                }
                if (!details.empty()) sora_label += " (" + details + ")";
            }
            auto blocking = truth(field(ui, "blocking_ui_active"));
            std::string ui_details;
            for (const auto key : {"reason", "event"})
            {
                const auto value = text(field(ui, key));
                if (!value.empty()) { if (!ui_details.empty()) ui_details += ", "; ui_details += value; }
            }
            const auto callback = number(field(ui, "callback_id"));
            if (!callback.empty()) { if (!ui_details.empty()) ui_details += ", "; ui_details += "id=" + callback; }
            if (blocking != "-" && !ui_details.empty()) blocking += " (" + ui_details + ")";
            auto request = std::string{"No native request state yet"};
            if (text(field(readiness, "schema")) == "kh3ap.item_delivery_readiness.v1")
            {
                request = text(field(readiness, "status"));
                const auto reason = text(field(readiness, "reason"));
                if (!reason.empty()) request += ": " + reason;
            }
            Json bound_minimap = Json::object_t{};
            const auto pawn = text(field(context, "object"));
            if (!asset.empty() && field(minimap, "process_id").is_number() &&
                field(minimap, "process_id").get<double>() == pid &&
                text(field(context_map(minimap), "asset_path")) == asset &&
                !pawn.empty() && pawn.substr(0, pawn.find(" @")) == text(field(minimap, "pawn")) &&
                text(field(minimap, "texture_path")).starts_with("/Game/UI/Icon/Map/")) bound_minimap = minimap;
            return Json{{"schema", "kh3ap.runtime_status.v1"}, {"owner", "dll"}, {"process_id", pid},
                {"seed_name", std::string(seed)}, {"slot", slot}, {"delivery_epoch", epoch}, {"map", std::move(map)},
                {"context", source}, {"minimap", std::move(bound_minimap)},
                {"presentation", Json{{"blocking_ui", blocking}, {"special_battle_mode", truth(field(sora, "special_battle_mode_active"))},
                    {"sora_state", sora_label}, {"runtime_context", object_summary(context)}, {"current_map", map_label},
                    {"map_safe", safety}, {"safe_context", request}, {"ap_items_grantable", ready ? "DLL" : "Unavailable"}}}};
        }
    };
}
