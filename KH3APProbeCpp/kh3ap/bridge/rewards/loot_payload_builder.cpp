#include "kh3ap/bridge/rewards/loot_payload_builder.h"

namespace kh3ap
{
    auto RuntimeLootPayloadBuilder::build_event_payload(
            std::wstring_view bridge_key,
            std::wstring_view actor_name,
            std::wstring_view token_field_name,
            std::wstring_view token_value) -> RuntimeLootPayload
    {
        return RuntimeLootPayload{
                .type = std::wstring{L"event"},
                .bridge_key = std::wstring{bridge_key},
                .actor_name = std::wstring{actor_name},
                .source_id = std::wstring{token_value},
                .source = std::wstring{L"UTresGameBlueprintLibrary::BP_TresGetItem"},
                .grant_context = std::wstring{L"scripted_event"},
                .encounter_hint = std::wstring{actor_name},
                .mapping_reason = std::wstring{L"resolved from slot_data.json event vanilla_item"},
                .resolution_kind = std::wstring{L"item_direct"},
                .token_field_name = std::wstring{token_field_name},
                .token_value = std::wstring{token_value},
        };
    }

    auto RuntimeLootPayloadBuilder::resolve_vbonus_runtime_token(std::wstring_view token_value) -> VBonusRuntimeTokenResolution
    {
        VBonusRuntimeTokenResolution resolution{
                .effective_token = std::wstring{token_value},
                .resolution_kind = std::wstring{L"unknown"},
        };

        constexpr std::wstring_view compact_prefix = L"compact:";
        constexpr std::wstring_view pointer_prefix = L"pointer:";
        constexpr std::wstring_view actor_prefix = L"actor:";

        if (resolution.effective_token.starts_with(compact_prefix))
        {
            resolution.effective_token = resolution.effective_token.substr(compact_prefix.size());
            resolution.resolution_kind = L"compact_direct";
        }
        else if (resolution.effective_token.starts_with(pointer_prefix))
        {
            resolution.effective_token = resolution.effective_token.substr(pointer_prefix.size());
            resolution.resolution_kind = L"pointer_like";
        }
        else if (resolution.effective_token.starts_with(actor_prefix))
        {
            resolution.effective_token = resolution.effective_token.substr(actor_prefix.size());
            resolution.resolution_kind = L"actor_fallback";
        }

        return resolution;
    }

    auto RuntimeLootPayloadBuilder::build_vbonus_payload(
            std::wstring_view bridge_key,
            std::wstring_view actor_name,
            std::wstring_view source,
            std::wstring_view token_field_name,
            const VBonusRuntimeTokenResolution& token_resolution) -> RuntimeLootPayload
    {
        return RuntimeLootPayload{
                .type = std::wstring{L"vbonus"},
                .bridge_key = std::wstring{bridge_key},
                .actor_name = std::wstring{actor_name},
                .source_id = token_resolution.effective_token,
                .source = std::wstring{source},
                .grant_context = std::wstring{L"fight_vbonus"},
                .mapping_reason = std::wstring{L"resolved from slot_data.json vbonus source_id via runtime FName normalization"},
                .resolution_kind = token_resolution.resolution_kind,
                .token_field_name = std::wstring{token_field_name},
                .token_value = token_resolution.effective_token,
        };
    }

    auto RuntimeLootPayloadBuilder::build_vbonus_event_array_removed_payload(
            std::wstring_view bridge_key,
            std::wstring_view source,
            std::wstring_view entry) -> RuntimeLootPayload
    {
        return RuntimeLootPayload{
                .type = std::wstring{L"vbonus"},
                .bridge_key = std::wstring{bridge_key},
                .actor_name = std::wstring{source},
                .source_id = std::wstring{entry},
                .source = std::wstring{L"EventPersistanceData_C.notObtainedVBonusData"},
                .grant_context = std::wstring{L"fight_vbonus"},
                .mapping_reason = std::wstring{L"resolved from deferred removal in pending vbonus pool"},
                .resolution_kind = std::wstring{L"event_array_removed"},
                .token_field_name = std::wstring{L"notObtainedVBonusData"},
                .token_value = std::wstring{entry},
        };
    }

    auto RuntimeLootPayloadBuilder::build_treasure_payload(
            std::wstring_view full_name,
            const TreasureMappingResult& mapping,
            std::wstring_view level_path,
            std::wstring_view resolution_kind,
            std::wstring_view token_field_name,
            std::wstring_view treasure_id,
            std::optional<bool> is_large) -> RuntimeLootPayload
    {
        return RuntimeLootPayload{
                .type = std::wstring{L"treasure"},
                .bridge_key = mapping.bridge_key,
                .actor_name = std::wstring{full_name},
                .world_code = mapping.world_code,
                .source_id = mapping.source_id,
                .source = std::wstring{L"ue4ss_runtime_cpp"},
                .level_path = std::wstring{level_path},
                .grant_context = std::wstring{L"treasure_chest"},
                .encounter_hint = std::wstring{full_name},
                .mapping_reason = mapping.mapping_reason,
                .resolution_kind = std::wstring{resolution_kind},
                .token_field_name = std::wstring{token_field_name},
                .token_value = mapping.source_id,
                .treasure_id = std::wstring{treasure_id},
                .is_large = is_large,
        };
    }
}
