#pragma once

#include "kh3ap/core/types.h"
#include "kh3ap/bridge/rewards/vbonus_reward_bundle.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kh3ap
{
    class RuntimeVBonusResolver
    {
    public:
        static auto find_manifest_entry(
                std::wstring_view source_id,
                const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup)
                -> std::optional<VBonusRewardBundleEntry>;

        static auto find_bridge_key_for_source_id(
                std::wstring_view source_id,
                const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup,
                const std::unordered_map<std::wstring, std::wstring>& slot_data_lookup)
                -> std::optional<std::wstring>;

        static auto log_manifest_summary(
                std::wstring_view source_id,
                std::wstring_view resolution_source,
                const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup) -> void;

        static auto apply_manifest_metadata(
                RuntimeLootPayload& payload,
                const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup) -> void;

        static auto find_source_id_for_runtime_reward_token(
                std::wstring_view runtime_reward_token,
                const std::unordered_map<std::wstring, std::wstring>& runtime_reward_lookup)
                -> std::optional<std::wstring>;

    };
}
