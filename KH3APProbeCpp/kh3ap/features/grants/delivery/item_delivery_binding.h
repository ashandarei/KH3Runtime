#pragma once
#include "kh3ap/bridge/serialization/json_fields.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap::item_delivery
{
    struct DeliverySeedIdentity
    {
        std::wstring seed;
        int64_t slot{};
    };

    inline auto installed_delivery_seed_identity() -> DeliverySeedIdentity
    {
        const auto identity = JsonObject::read_seed("slot_data.json").object_field("kh3_seed");
        return {identity.string_field("seed_name").value_or(L""), identity.int64_field("slot").value_or(0)};
    }

    // Separate from gameplay readiness: never recover a pawn for work belonging
    // to another process or seed. Empty metadata is a rejection, not a fallback.
    inline auto delivery_binding_error(std::wstring_view schema, int64_t request_pid,
            int64_t current_pid, std::wstring_view seed, int64_t slot,
            std::wstring_view status_seed, int64_t status_slot,
            std::wstring_view slot_seed, int64_t slot_slot) -> std::wstring_view
    {
        if (schema != L"kh3ap.item_delivery.v1") return L"request_schema_mismatch";
        if (request_pid <= 0 || request_pid != current_pid) return L"request_process_mismatch";
        if (seed.empty() || slot <= 0 || seed != status_seed || seed != slot_seed ||
            slot != status_slot || slot != slot_slot) return L"request_seed_mismatch";
        return {};
    }
}
