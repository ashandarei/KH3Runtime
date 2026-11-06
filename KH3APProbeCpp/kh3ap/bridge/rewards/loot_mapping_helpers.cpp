#include "kh3ap/bridge/rewards/loot_mapping_helpers.h"

#include "kh3ap/bridge/catalog/bridge_lookup_store.h"
#include "kh3ap/features/grants/events/event_resolver.h"
#include "kh3ap/features/player/context/context_resolver.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/bridge/rewards/loot_payload_builder.h"
#include "kh3ap/bridge/rewards/loot_payload_store.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/vbonus/vbonus_resolver.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>

#include <utility>

namespace kh3ap
{
    auto build_unique_event_bridge_key_lookup(
            const std::optional<std::filesystem::path>& bridge_dir,
            bool debug_logging) -> RuntimeBridgeKeyLookup
    {
        RuntimeBridgeKeyLookup unique_lookup{};
        if (!bridge_dir.has_value())
        {
            if (debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(STR("[KH3APProbeCpp] Event lookup build aborted reason=no_bridge_dir\n"));
            }
            return unique_lookup;
        }

        if (debug_logging)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Event lookup using bridge_dir={}\n"),
                    bridge_dir->wstring());
        }

        const auto slot_data_path = *bridge_dir / "slot_data.json";
        if (debug_logging)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Event lookup reading slot_data_path={}\n"),
                    slot_data_path.wstring());
        }

        auto parse_result = BridgeLookupStore::build_unique_event_bridge_key_lookup(*bridge_dir);
        unique_lookup = std::move(parse_result.lookup);

        if (debug_logging)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Event lookup table built unique_entries={} ambiguous_tokens={}\n"),
                    unique_lookup.size(),
                    parse_result.ambiguous_count);
        }

        return unique_lookup;
    }

    auto build_typed_source_bridge_key_lookup(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::string_view type) -> RuntimeBridgeKeyLookup
    {
        if (!bridge_dir.has_value())
        {
            return {};
        }

        return BridgeLookupStore::build_typed_source_bridge_key_lookup(*bridge_dir, type);
    }

    auto build_vbonus_reward_bundle_lookup(
            const std::optional<std::filesystem::path>& bridge_dir) -> RuntimeVBonusRewardBundleLookup
    {
        if (!bridge_dir.has_value())
        {
            return {};
        }

        return BridgeLookupStore::build_vbonus_reward_bundle_lookup(*bridge_dir);
    }

    auto build_unique_vbonus_runtime_reward_lookup(
            const std::optional<std::filesystem::path>& bridge_dir) -> RuntimeBridgeKeyLookup
    {
        if (!bridge_dir.has_value())
        {
            return {};
        }

        return BridgeLookupStore::build_unique_vbonus_runtime_reward_lookup(*bridge_dir);
    }

    auto find_event_bridge_key_for_vanilla_item(
            std::wstring_view vanilla_item,
            const RuntimeBridgeKeyLookup& lookup,
            bool debug_logging) -> std::optional<std::wstring>
    {
        return RuntimeEventResolver::find_bridge_key_for_vanilla_item(vanilla_item, lookup, debug_logging);
    }

    auto is_twilight_town_runtime_context(
            const std::optional<RuntimeContextSnapshot>& runtime_context) -> bool
    {
        return runtime_context.has_value() &&
               RuntimeContextResolver::is_twilight_town_context(*runtime_context);
    }

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
            std::wstring_view runtime_context_summary) -> void
    {
        if (!enabled)
        {
            return;
        }
        if (phase != HookPhase::Pre ||
            hook_info.function_name != bp_get_item_name ||
            !target_function ||
            !params ||
            RuntimeGrantInternalCallDepth::is_active())
        {
            return;
        }

        const bool is_target_token =
                token_value == STR("LSIGAME01") ||
                token_value == STR("WEP_KEYBLADE_SO_02");
        if (!is_target_token)
        {
            return;
        }

        if (!is_twilight_town_runtime_context(runtime_context))
        {
            return;
        }

        RC::Unreal::FProperty* const show_property = find_function_param_by_name(target_function, STR("bShowInfo"));
        if (!show_property)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] TTBistroPopupSuppress failed token={} reason=no_bShowInfo context={} | {}\n"),
                    std::wstring{token_value},
                    std::wstring{context_name},
                    std::wstring{runtime_context_summary});
            return;
        }

        const bool wrote = write_bool_function_param(show_property, params, false);
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] TTBistroPopupSuppress token={} wrote={} context={} | {}\n"),
                std::wstring{token_value},
                wrote ? STR("true") : STR("false"),
                std::wstring{context_name},
                std::wstring{runtime_context_summary});
    }

    auto find_vbonus_bridge_key_for_source_id(
            std::wstring_view source_id,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup,
            const RuntimeBridgeKeyLookup& slot_data_lookup) -> std::optional<std::wstring>
    {
        return RuntimeVBonusResolver::find_bridge_key_for_source_id(source_id, manifest_lookup, slot_data_lookup);
    }

    auto find_vbonus_reward_bundle_entry(
            std::wstring_view source_id,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup) -> std::optional<VBonusRewardBundleEntry>
    {
        return RuntimeVBonusResolver::find_manifest_entry(source_id, manifest_lookup);
    }

    auto log_vbonus_reward_bundle_summary(
            std::wstring_view source_id,
            std::wstring_view resolution_source,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup) -> void
    {
        RuntimeVBonusResolver::log_manifest_summary(source_id, resolution_source, manifest_lookup);
    }

    auto apply_vbonus_manifest_metadata(
            RuntimeLootPayload& payload,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup) -> void
    {
        RuntimeVBonusResolver::apply_manifest_metadata(payload, manifest_lookup);
    }

    auto find_vbonus_source_id_for_runtime_reward_token(
            std::wstring_view runtime_reward_token,
            const RuntimeBridgeKeyLookup& runtime_reward_lookup) -> std::optional<std::wstring>
    {
        return RuntimeVBonusResolver::find_source_id_for_runtime_reward_token(runtime_reward_token, runtime_reward_lookup);
    }

    auto try_emit_runtime_payload_once(
            const std::vector<std::filesystem::path>& output_paths,
            const RuntimeLootPayload& payload) -> bool
    {
        return RuntimeLootPayloadStore::emit_once(output_paths, payload);
    }

    auto should_emit_runtime_payload(
            const RuntimeLootPayload& payload,
            const std::optional<RuntimeContextSnapshot>& runtime_context,
            bool debug_logging) -> bool
    {
        return RuntimeLootPayloadStore::should_emit(payload, runtime_context, debug_logging);
    }

    auto try_emit_vbonus_payload_from_event_array_removed(
            std::wstring_view source,
            std::wstring_view snapshot_key,
            std::wstring_view entry,
            const RuntimeVBonusRewardBundleLookup& manifest_lookup,
            const RuntimeBridgeKeyLookup& vbonus_bridge_key_lookup,
            const RuntimePayloadPredicate& should_emit,
            const RuntimePayloadEmitter& emit_payload) -> void
    {
        if (snapshot_key != STR("EventPersistanceData_C|notObtainedVBonusData"))
        {
            return;
        }

        if (!entry.starts_with(STR("Vbonus_")))
        {
            return;
        }

        log_vbonus_reward_bundle_summary(entry, source, manifest_lookup);

        const auto bridge_key = find_vbonus_bridge_key_for_source_id(entry, manifest_lookup, vbonus_bridge_key_lookup);
        if (!bridge_key.has_value())
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] VBonus removal payload skip source={} token={} reason=no_bridge_key\n"),
                    source,
                    entry);
            return;
        }

        RuntimeLootPayload payload = RuntimeLootPayloadBuilder::build_vbonus_event_array_removed_payload(
                *bridge_key,
                source,
                entry);
        apply_vbonus_manifest_metadata(payload, manifest_lookup);

        if (should_emit && !should_emit(payload))
        {
            return;
        }

        if (emit_payload)
        {
            emit_payload(payload);
        }
    }

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
            bool debug_logging) -> std::optional<RuntimeLootPayload>
    {
        if (hook_name == bp_get_item_name)
        {
            const auto bridge_key = find_event_bridge_key_for_vanilla_item(token_value, event_bridge_key_lookup, debug_logging);
            if (!bridge_key.has_value())
            {
                if (debug_logging)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[KH3APProbeCpp] Payload skip hook={} token={} reason=no_bridge_key\n"),
                            std::wstring{hook_name},
                            std::wstring{token_value});
                }
                return std::nullopt;
            }

            if (debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Payload build hook={} token={} bridge_key={}\n"),
                        std::wstring{hook_name},
                        std::wstring{token_value},
                        *bridge_key);
            }
            return RuntimeLootPayloadBuilder::build_event_payload(
                    *bridge_key,
                    actor_name,
                    token_field_name,
                    token_value);
        }

        if (hook_name == bp_get_vbonus_name || hook_name == wait_bp_get_vbonus_name)
        {
            const auto token_resolution = RuntimeLootPayloadBuilder::resolve_vbonus_runtime_token(token_value);

            log_vbonus_reward_bundle_summary(token_resolution.effective_token, hook_name, manifest_lookup);

            const auto bridge_key = find_vbonus_bridge_key_for_source_id(
                    token_resolution.effective_token,
                    manifest_lookup,
                    vbonus_bridge_key_lookup);
            if (!bridge_key.has_value())
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] VBonus payload skip token={} reason=no_bridge_key\n"),
                        token_resolution.effective_token);
                return std::nullopt;
            }

            RuntimeLootPayload payload = RuntimeLootPayloadBuilder::build_vbonus_payload(
                    *bridge_key,
                    actor_name,
                    hook_name,
                    token_field_name,
                    token_resolution);
            apply_vbonus_manifest_metadata(payload, manifest_lookup);
            return payload;
        }

        return std::nullopt;
    }
}
