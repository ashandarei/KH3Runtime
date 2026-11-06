#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kh3ap
{
    class JsonObject;
    struct UniqueLookupParseResult
    {
        std::unordered_map<std::wstring, std::wstring> lookup{};
        size_t ambiguous_count{0};
    };

    struct ShopItemBridgeLocation
    {
        std::wstring bridge_key{};
        std::wstring source_id{};
        std::wstring item_token{};
        int32_t shop_price{0};
    };

    class SlotDataLookupParser
    {
    public:
        static auto unique_event_bridge_key_lookup(const JsonObject& document) -> UniqueLookupParseResult;
        static auto typed_source_bridge_key_lookup(const JsonObject& document, std::string_view type) -> std::unordered_map<std::wstring, std::wstring>;
        static auto shop_item_locations(const JsonObject& document) -> std::vector<ShopItemBridgeLocation>;
        static auto unique_vbonus_runtime_reward_lookup(const JsonObject& document) -> std::unordered_map<std::wstring, std::wstring>;
        static auto bool_option(const JsonObject& document, std::string_view option_name) -> std::optional<bool>;
        static auto int_option(const JsonObject& document, std::string_view option_name) -> std::optional<int>;
        static auto unique_event_bridge_key_lookup(const std::string& slot_data_text) -> UniqueLookupParseResult;
        static auto typed_source_bridge_key_lookup(const std::string& slot_data_text, std::string_view type) -> std::unordered_map<std::wstring, std::wstring>;
        static auto shop_item_locations(const std::string& slot_data_text) -> std::vector<ShopItemBridgeLocation>;
        static auto unique_vbonus_runtime_reward_lookup(const std::string& slot_data_text) -> std::unordered_map<std::wstring, std::wstring>;
        static auto bool_option(const std::string& slot_data_text, std::string_view option_name) -> std::optional<bool>;

        // For Choice options, whose slot_data value is the option index
        // rather than a bool.
        static auto int_option(const std::string& slot_data_text, std::string_view option_name) -> std::optional<int>;

    private:
        static auto vbonus_runtime_reward_aliases(std::wstring_view vanilla_item) -> std::vector<std::wstring>;
    };
}
