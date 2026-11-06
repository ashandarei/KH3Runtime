#include "kh3ap/bridge/rewards/vbonus_reward_bundle.h"
#include "kh3ap/bridge/serialization/json_fields.h"

namespace kh3ap
{
    auto VBonusRewardBundleParser::parse_lookup(const JsonObject& seed_slot_data)
        -> std::unordered_map<std::wstring, VBonusRewardBundleEntry>
    {
        std::unordered_map<std::wstring, VBonusRewardBundleEntry> lookup;
        for (const auto& row : seed_slot_data.objects_field("runtime_reward_bundles"))
        {
            const auto source = row.string_field("source_id");
            if (row.string_field("kind") != L"vbonus" || !source || source->empty()) continue;
            lookup.emplace(*source, VBonusRewardBundleEntry{
                .source_id = *source,
                .bridge_key = row.string_field("bridge_key").value_or(L""),
                .location_name = row.string_field("location_name").value_or(L""),
                .world = row.string_field("world").value_or(L""),
                .grant_context = row.string_field("grant_context").value_or(L""),
                .encounter_hint = row.string_field("encounter_hint").value_or(L""),
                .randomized_tokens_json = row.array_json_field("randomized_tokens").value_or(L""),
                .vanilla_tokens_json = row.array_json_field("vanilla_tokens").value_or(L""),
                .randomized_summary = row.string_field("randomized_summary").value_or(L""),
                .vanilla_summary = row.string_field("vanilla_summary").value_or(L""),
            });
        }
        return lookup;
    }
}
