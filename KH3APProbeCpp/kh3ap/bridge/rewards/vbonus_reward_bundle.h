#pragma once

#include "kh3ap/bridge/serialization/json_fields.h"
#include <string>
#include <unordered_map>

namespace kh3ap
{
    struct VBonusRewardBundleEntry
    {
        std::wstring source_id;
        std::wstring bridge_key;
        std::wstring location_name;
        std::wstring world;
        std::wstring grant_context;
        std::wstring encounter_hint;
        std::wstring randomized_tokens_json;
        std::wstring vanilla_tokens_json;
        std::wstring randomized_summary;
        std::wstring vanilla_summary;
    };

    class VBonusRewardBundleParser
    {
    public:
        static auto parse_lookup(const JsonObject& seed_slot_data) -> std::unordered_map<std::wstring, VBonusRewardBundleEntry>;
    };
}
