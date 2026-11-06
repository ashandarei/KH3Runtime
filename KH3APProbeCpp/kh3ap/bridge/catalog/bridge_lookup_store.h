#pragma once

#include "kh3ap/bridge/catalog/slot_data_lookup_parser.h"
#include "kh3ap/features/player/keyblades/style_change_mode.h"
#include "kh3ap/bridge/rewards/vbonus_reward_bundle.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kh3ap
{
    class BridgeLookupStore
    {
    public:
        static auto build_unique_event_bridge_key_lookup(const std::filesystem::path& bridge_dir) -> UniqueLookupParseResult;

        static auto build_typed_source_bridge_key_lookup(
                const std::filesystem::path& bridge_dir,
                std::string_view type) -> std::unordered_map<std::wstring, std::wstring>;

        static auto build_vbonus_reward_bundle_lookup(
                const std::filesystem::path& bridge_dir) -> std::unordered_map<std::wstring, VBonusRewardBundleEntry>;

        static auto build_unique_vbonus_runtime_reward_lookup(
                const std::filesystem::path& bridge_dir) -> std::unordered_map<std::wstring, std::wstring>;

    };
}
