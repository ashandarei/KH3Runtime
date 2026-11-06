#include "kh3ap/bridge/catalog/slot_data_lookup_parser.h"
#include "kh3ap/bridge/serialization/json_fields.h"

#include <limits>
#include <set>

namespace kh3ap
{
    namespace
    {
        auto locations(const JsonObject& document) -> std::vector<JsonObject>
        { return document.object_field("bridge").objects_field("locations"); }

        auto insert_unique(std::unordered_map<std::wstring, std::wstring>& lookup,
                           std::set<std::wstring>& ambiguous,
                           const std::wstring& key, const std::wstring& value) -> void
        {
            if (ambiguous.contains(key)) return;
            const auto [found, inserted] = lookup.emplace(key, value);
            if (!inserted && found->second != value)
            {
                lookup.erase(found);
                ambiguous.insert(key);
            }
        }
    }

    auto SlotDataLookupParser::unique_event_bridge_key_lookup(const JsonObject& document) -> UniqueLookupParseResult
    {
        UniqueLookupParseResult result{};
        std::set<std::wstring> ambiguous;
        for (const auto& location : locations(document))
        {
            const auto key = location.string_field("bridge_key");
            const auto vanilla = location.string_field("vanilla_item");
            if (location.string_field("type") == L"event" && key && !key->empty() && vanilla && !vanilla->empty())
                insert_unique(result.lookup, ambiguous, *vanilla, *key);
        }
        result.ambiguous_count = ambiguous.size();
        return result;
    }

    auto SlotDataLookupParser::typed_source_bridge_key_lookup(const JsonObject& document, std::string_view type)
        -> std::unordered_map<std::wstring, std::wstring>
    {
        std::unordered_map<std::wstring, std::wstring> lookup;
        const std::wstring expected{type.begin(), type.end()};
        for (const auto& location : locations(document))
        {
            const auto key = location.string_field("bridge_key");
            const auto source = location.string_field("source_id");
            if (location.string_field("type") == expected && key && !key->empty() && source && !source->empty())
                lookup.emplace(*source, *key);
        }
        return lookup;
    }

    auto SlotDataLookupParser::shop_item_locations(const JsonObject& document) -> std::vector<ShopItemBridgeLocation>
    {
        std::vector<ShopItemBridgeLocation> result;
        for (const auto& location : locations(document))
        {
            const auto key = location.string_field("bridge_key");
            const auto source = location.string_field("source_id");
            const auto token = location.string_field("shop_item_token");
            const auto price = location.int64_field("shop_price");
            const int32_t shop_price = price && *price > 0 && *price <= std::numeric_limits<int32_t>::max()
                ? static_cast<int32_t>(*price) : 0;
            if (location.string_field("type") == L"shop" && key && !key->empty() && source && !source->empty() && token && !token->empty())
                result.push_back({.bridge_key = *key, .source_id = *source, .item_token = *token,
                    .shop_price = shop_price});
        }
        return result;
    }

    auto SlotDataLookupParser::vbonus_runtime_reward_aliases(std::wstring_view vanilla_item) -> std::vector<std::wstring>
    {
        for (const auto prefix : {L"ETresAbilityKind::", L"ETresVictoryBonusKind::"})
        {
            const std::wstring_view prefix_view{prefix};
            if (vanilla_item.starts_with(prefix_view))
            {
                const auto suffix = vanilla_item.substr(prefix_view.size());
                if (!suffix.empty() && suffix != L"NONE") return {std::wstring{L"GIVESORA_"} + std::wstring{suffix}};
            }
        }
        return {};
    }

    auto SlotDataLookupParser::unique_vbonus_runtime_reward_lookup(const JsonObject& document)
        -> std::unordered_map<std::wstring, std::wstring>
    {
        std::unordered_map<std::wstring, std::wstring> lookup;
        std::set<std::wstring> ambiguous;
        for (const auto& location : locations(document))
        {
            const auto source = location.string_field("source_id");
            if (location.string_field("type") != L"vbonus" || !source || source->empty()) continue;
            const auto vanilla = location.object_field("vanilla_items");
            for (const auto field : {"Sora_Ability1", "Sora_Ability2", "Sora_Bonus1", "Sora_Bonus2"})
                if (const auto value = vanilla.string_field(field))
                    for (const auto& alias : vbonus_runtime_reward_aliases(*value))
                        insert_unique(lookup, ambiguous, alias, *source);
        }
        return lookup;
    }

    auto SlotDataLookupParser::int_option(const JsonObject& document, std::string_view name) -> std::optional<int>
    {
        const auto value = document.int64_field(name);
        if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) return std::nullopt;
        return static_cast<int>(*value);
    }

    auto SlotDataLookupParser::bool_option(const JsonObject& document, std::string_view name) -> std::optional<bool>
    {
        if (const auto value = document.bool_field(name)) return value;
        const auto value = document.int64_field(name);
        if (value == 0) return false;
        if (value == 1) return true;
        return std::nullopt;
    }

    auto SlotDataLookupParser::unique_event_bridge_key_lookup(const std::string& text) -> UniqueLookupParseResult
    { return unique_event_bridge_key_lookup(JsonObject{text}); }
    auto SlotDataLookupParser::typed_source_bridge_key_lookup(const std::string& text, std::string_view type)
        -> std::unordered_map<std::wstring, std::wstring>
    { return typed_source_bridge_key_lookup(JsonObject{text}, type); }
    auto SlotDataLookupParser::shop_item_locations(const std::string& text) -> std::vector<ShopItemBridgeLocation>
    { return shop_item_locations(JsonObject{text}); }
    auto SlotDataLookupParser::unique_vbonus_runtime_reward_lookup(const std::string& text)
        -> std::unordered_map<std::wstring, std::wstring>
    { return unique_vbonus_runtime_reward_lookup(JsonObject{text}); }
    auto SlotDataLookupParser::int_option(const std::string& text, std::string_view name) -> std::optional<int>
    {
        const auto value = JsonFieldReader::int64_field(text, name);
        if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) return std::nullopt;
        return static_cast<int>(*value);
    }
    auto SlotDataLookupParser::bool_option(const std::string& text, std::string_view name) -> std::optional<bool>
    {
        if (const auto value = JsonFieldReader::bool_field(text, name)) return value;
        const auto value = JsonFieldReader::int64_field(text, name);
        if (value == 0) return false;
        if (value == 1) return true;
        return std::nullopt;
    }
}
