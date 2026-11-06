#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    using AbilityProbeJsonlEmitter = void (*)(
            std::wstring_view kind,
            std::wstring_view reason,
            std::wstring_view object_name,
            std::wstring_view class_name,
            std::wstring_view property_name,
            std::wstring_view property_class,
            int offset,
            int element_size,
            std::wstring_view value);

    auto log_targeted_runtime_ability_query_snapshot(
            std::wstring_view reason,
            std::wstring_view runtime_token,
            RC::Unreal::UObject* preferred_pawn_object,
            RuntimeState& runtime_state,
            bool enabled,
            std::wstring_view runtime_context_summary,
            const std::array<std::pair<std::wstring_view, std::wstring_view>, 3>& query_functions,
            AbilityProbeJsonlEmitter emit_probe_jsonl) -> void;
}
