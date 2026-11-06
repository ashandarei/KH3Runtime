#include "kh3ap/bridge/catalog/bridge_lookup_store.h"

#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"


namespace kh3ap
{
    auto BridgeLookupStore::build_unique_event_bridge_key_lookup(
            const std::filesystem::path& bridge_dir) -> UniqueLookupParseResult
    {
        const auto document = JsonObject::read_seed(bridge_dir / "slot_data.json");
        if (!document.valid())
        {
            return {};
        }

        return SlotDataLookupParser::unique_event_bridge_key_lookup(document);
    }

    auto BridgeLookupStore::build_typed_source_bridge_key_lookup(
            const std::filesystem::path& bridge_dir,
            std::string_view type) -> std::unordered_map<std::wstring, std::wstring>
    {
        const auto document = JsonObject::read_seed(bridge_dir / "slot_data.json");
        if (!document.valid())
        {
            return {};
        }

        return SlotDataLookupParser::typed_source_bridge_key_lookup(document, type);
    }

    auto BridgeLookupStore::build_vbonus_reward_bundle_lookup(
            const std::filesystem::path& bridge_dir) -> std::unordered_map<std::wstring, VBonusRewardBundleEntry>
    {
        const auto document = JsonObject::read_seed(bridge_dir / "slot_data.json");
        return VBonusRewardBundleParser::parse_lookup(document);
    }

    auto BridgeLookupStore::build_unique_vbonus_runtime_reward_lookup(
            const std::filesystem::path& bridge_dir) -> std::unordered_map<std::wstring, std::wstring>
    {
        const auto document = JsonObject::read_seed(bridge_dir / "slot_data.json");
        if (!document.valid())
        {
            return {};
        }

        return SlotDataLookupParser::unique_vbonus_runtime_reward_lookup(document);
    }

}
