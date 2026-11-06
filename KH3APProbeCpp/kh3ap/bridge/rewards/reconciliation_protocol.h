#pragma once
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/rewards/receipts.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap::reconciliation::protocol
{
    using Json = kh3ap::json::Value;
    auto utf8_to_wstring(std::string_view value) -> std::wstring;
    auto wstring_to_utf8(std::wstring_view value) -> std::string;
    auto field(const Json& value, std::string_view key) -> const Json&;
    auto text(const Json& value) -> std::string;
    auto number(const Json& value, int64_t fallback = -1) -> int64_t;
    auto flag(const Json& value) -> bool;
    auto encode(const Json& value) -> std::string;
    auto decode(std::string_view value, size_t max_bytes = json::limits::message_bytes) -> Json;
    auto array(const Json& value) -> const Json::array_t&;
    auto receipt_json(const Receipts& receipts) -> Json;
    auto receipts(const Json& value) -> Receipts;
    auto utc(uint64_t ticks = 0) -> std::string;
    auto slot_path(int64_t slot) -> std::string;
    auto map_path(const Json& context) -> std::string;
} // namespace kh3ap::reconciliation::protocol
