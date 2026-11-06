#pragma once
#include "kh3ap/features/grants/delivery/item_delivery_catalog.h"
#include "kh3ap/bridge/serialization/json.h"
#include <map>
#include <string>
#include <vector>

namespace kh3ap::native_catalog
{
    // Resolve compact AP receipts against the same authoritative catalog as delivery.
    auto runtime_token_for_item(int64_t item_id) -> std::wstring;
    enum class CheckKind { Event, VBonus, Chest };
    struct CheckRow { std::wstring source_id, flag_name; int64_t threshold{-1}; bool requires_tt_clear{}; };
    struct CheckSetup { std::wstring seed_name; int64_t slot{}; std::vector<CheckRow> rows; };
    auto check_setup(std::string_view slot_data, CheckKind kind) -> CheckSetup;
    // Describe the exact decoded/expanded rewards used by native delivery.
    auto describe(const kh3ap::json::Value& receipt, const item_delivery::ReceiptTemplate& item,
            const std::vector<RuntimeItemGrantRequest>& grants) -> kh3ap::json::Value;

    // Worker-owned seed facts. No UObject access or game-thread reflection.
    class Seed
    {
        kh3ap::json::Value data;
        std::map<int64_t, kh3ap::json::Value> locations;
        std::map<std::string, kh3ap::json::Value> bundles;
        auto reward(std::string_view token, const kh3ap::json::Value& receipt) const -> item_delivery::RewardTemplate;
    public:
        void configure(const kh3ap::json::Value& slot_data);
        auto decode(const kh3ap::json::Value& receipt) const -> item_delivery::ReceiptTemplate;
    };
}
