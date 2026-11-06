#pragma once

#include "kh3ap/core/types.h"
#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/bridge/rewards/vbonus_reward_bundle.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RC::Unreal
{
    class UFunction;
}

namespace kh3ap
{
    using RuntimeBridgeKeyLookup = std::unordered_map<std::wstring, std::wstring>;
    using RuntimeVBonusRewardBundleLookup = std::unordered_map<std::wstring, VBonusRewardBundleEntry>;
    using RuntimePayloadPredicate = std::function<bool(const RuntimeLootPayload&)>;
    using RuntimePayloadEmitter = std::function<bool(const RuntimeLootPayload&)>;

    auto build_unique_event_bridge_key_lookup(
            const std::optional<std::filesystem::path>& bridge_dir,
            bool debug_logging) -> RuntimeBridgeKeyLookup;

    auto build_typed_source_bridge_key_lookup(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::string_view type) -> RuntimeBridgeKeyLookup;

    auto build_vbonus_reward_bundle_lookup(
            const std::optional<std::filesystem::path>& bridge_dir) -> RuntimeVBonusRewardBundleLookup;

    auto build_unique_vbonus_runtime_reward_lookup(
            const std::optional<std::filesystem::path>& bridge_dir) -> RuntimeBridgeKeyLookup;

    auto find_event_bridge_key_for_vanilla_item(
            std::wstring_view vanilla_item,
            const RuntimeBridgeKeyLookup& lookup,
            bool debug_logging) -> std::optional<std::wstring>;

    auto is_twilight_town_runtime_context(
            const std::optional<RuntimeContextSnapshot>& runtime_context) -> bool;

    auto maybe_suppress_twilight_town_bistro_reward_popup(
            bool enabled,
            const HookTargetInfo& hook_info,
            HookPhase phase,
            RC::Unreal::UFunction* target_function,
            void* params,
            std::wstring_view token_value,
            std::wstring_view context_name,
            std::wstring_view bp_get_item_name,
            const std::optional<RuntimeContextSnapshot>& runtime_context,
            std::wstring_view runtime_context_summary) -> void;

    auto find_vbonus_bridge_key_for_source_id(
            std::wstring_view source_id,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup,
            const RuntimeBridgeKeyLookup& slot_data_lookup) -> std::optional<std::wstring>;

    auto find_vbonus_reward_bundle_entry(
            std::wstring_view source_id,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup) -> std::optional<VBonusRewardBundleEntry>;

    auto log_vbonus_reward_bundle_summary(
            std::wstring_view source_id,
            std::wstring_view resolution_source,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup) -> void;

    auto apply_vbonus_manifest_metadata(
            RuntimeLootPayload& payload,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup) -> void;

    auto find_vbonus_source_id_for_runtime_reward_token(
            std::wstring_view runtime_reward_token,
            const RuntimeBridgeKeyLookup& runtime_reward_lookup) -> std::optional<std::wstring>;

    auto try_emit_runtime_payload_once(
            const std::vector<std::filesystem::path>& output_paths,
            const RuntimeLootPayload& payload) -> bool;

    auto should_emit_runtime_payload(
            const RuntimeLootPayload& payload,
            const std::optional<RuntimeContextSnapshot>& runtime_context,
            bool debug_logging) -> bool;

    auto try_emit_vbonus_payload_from_event_array_removed(
            std::wstring_view source,
            std::wstring_view snapshot_key,
            std::wstring_view entry,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup,
            const RuntimeBridgeKeyLookup& vbonus_bridge_key_lookup,
            const RuntimePayloadPredicate& should_emit,
            const RuntimePayloadEmitter& emit_payload) -> void;

    auto try_build_runtime_payload(
            std::wstring_view hook_name,
            std::wstring_view token_field_name,
            std::wstring_view token_value,
            std::wstring_view actor_name,
            std::wstring_view bp_get_item_name,
            std::wstring_view bp_get_vbonus_name,
            std::wstring_view wait_bp_get_vbonus_name,
            const RuntimeBridgeKeyLookup& event_bridge_key_lookup,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup,
            const RuntimeBridgeKeyLookup& vbonus_bridge_key_lookup,
            bool debug_logging) -> std::optional<RuntimeLootPayload>;
}
