#pragma once

#include "kh3ap/core/types.h"

#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    struct VBonusRuntimeTokenResolution
    {
        std::wstring effective_token;
        std::wstring resolution_kind;
    };

    class RuntimeLootPayloadBuilder
    {
    public:
        static auto build_event_payload(
                std::wstring_view bridge_key,
                std::wstring_view actor_name,
                std::wstring_view token_field_name,
                std::wstring_view token_value) -> RuntimeLootPayload;

        static auto resolve_vbonus_runtime_token(std::wstring_view token_value) -> VBonusRuntimeTokenResolution;

        static auto build_vbonus_payload(
                std::wstring_view bridge_key,
                std::wstring_view actor_name,
                std::wstring_view source,
                std::wstring_view token_field_name,
                const VBonusRuntimeTokenResolution& token_resolution) -> RuntimeLootPayload;

        static auto build_vbonus_event_array_removed_payload(
                std::wstring_view bridge_key,
                std::wstring_view source,
                std::wstring_view entry) -> RuntimeLootPayload;

        static auto build_treasure_payload(
                std::wstring_view full_name,
                const TreasureMappingResult& mapping,
                std::wstring_view level_path,
                std::wstring_view resolution_kind,
                std::wstring_view token_field_name,
                std::wstring_view treasure_id,
                std::optional<bool> is_large) -> RuntimeLootPayload;
    };
}
