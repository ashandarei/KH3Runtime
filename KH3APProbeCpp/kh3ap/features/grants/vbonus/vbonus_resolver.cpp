#include "kh3ap/features/grants/vbonus/vbonus_resolver.h"

#include "kh3ap/bridge/text_helpers.h"

#include <DynamicOutput/DynamicOutput.hpp>

namespace kh3ap
{
    auto RuntimeVBonusResolver::find_manifest_entry(
            std::wstring_view source_id,
            const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup)
            -> std::optional<VBonusRewardBundleEntry>
    {
        if (const auto found = manifest_lookup.find(std::wstring{source_id}); found != manifest_lookup.end())
        {
            return found->second;
        }
        return std::nullopt;
    }

    auto RuntimeVBonusResolver::find_bridge_key_for_source_id(
            std::wstring_view source_id,
            const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup,
            const std::unordered_map<std::wstring, std::wstring>& slot_data_lookup)
            -> std::optional<std::wstring>
    {
        const auto manifest_entry = find_manifest_entry(source_id, manifest_lookup);
        if (manifest_entry.has_value() && !manifest_entry->bridge_key.empty())
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] VBonus bridge key source_id={} | bridge_key={} | via=seed_reward_bundle\n"),
                    source_id,
                    manifest_entry->bridge_key);
            return manifest_entry->bridge_key;
        }

        if (const auto found = slot_data_lookup.find(std::wstring{source_id}); found != slot_data_lookup.end())
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] VBonus bridge key source_id={} | bridge_key={} | via=slot_data\n"),
                    source_id,
                    found->second);
            return found->second;
        }

        if (manifest_entry.has_value())
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] VBonus bridge key miss source_id={} | manifest_present=true | manifest_bridge_key_empty={}\n"),
                    source_id,
                    manifest_entry->bridge_key.empty() ? STR("true") : STR("false"));
        }
        return std::nullopt;
    }

    auto RuntimeVBonusResolver::log_manifest_summary(
            std::wstring_view source_id,
            std::wstring_view resolution_source,
            const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup) -> void
    {
        const auto entry = find_manifest_entry(source_id, manifest_lookup);
        if (!entry.has_value())
        {
            return;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] VBonus manifest resolution_source={} | source_id={} | bridge_key={} | location={} | world={} | grant_context={} | encounter_hint={} | randomized_tokens={} | vanilla_tokens={} | randomized={} | vanilla={}\n"),
                resolution_source,
                entry->source_id,
                entry->bridge_key.empty() ? std::wstring{STR("<none>")} : entry->bridge_key,
                entry->location_name.empty() ? std::wstring{STR("<none>")} : entry->location_name,
                entry->world.empty() ? std::wstring{STR("<none>")} : entry->world,
                entry->grant_context.empty() ? std::wstring{STR("<none>")} : entry->grant_context,
                entry->encounter_hint.empty() ? std::wstring{STR("<none>")} : entry->encounter_hint,
                entry->randomized_tokens_json.empty() ? std::wstring{STR("[]")} : entry->randomized_tokens_json,
                entry->vanilla_tokens_json.empty() ? std::wstring{STR("[]")} : entry->vanilla_tokens_json,
                entry->randomized_summary.empty() ? std::wstring{STR("<none>")} : entry->randomized_summary,
                entry->vanilla_summary.empty() ? std::wstring{STR("<none>")} : entry->vanilla_summary);
    }

    auto RuntimeVBonusResolver::apply_manifest_metadata(
            RuntimeLootPayload& payload,
            const std::unordered_map<std::wstring, VBonusRewardBundleEntry>& manifest_lookup) -> void
    {
        const auto entry = find_manifest_entry(payload.source_id, manifest_lookup);
        if (!entry.has_value())
        {
            return;
        }
        if (payload.grant_context.empty())
        {
            payload.grant_context = entry->grant_context;
        }
        if (payload.encounter_hint.empty())
        {
            payload.encounter_hint = entry->encounter_hint;
        }
    }

    auto RuntimeVBonusResolver::find_source_id_for_runtime_reward_token(
            std::wstring_view runtime_reward_token,
            const std::unordered_map<std::wstring, std::wstring>& runtime_reward_lookup)
            -> std::optional<std::wstring>
    {
        const auto normalized = RuntimeTextHelpers::normalize_vbonus_actor_reward_token(runtime_reward_token);
        if (const auto found = runtime_reward_lookup.find(normalized); found != runtime_reward_lookup.end())
        {
            return found->second;
        }
        return std::nullopt;
    }

}
