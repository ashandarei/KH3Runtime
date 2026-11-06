#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class AbilityProbeDiagnosticState;
    class RuntimeState;

    struct RuntimeAbilityStateProbeDeps
    {
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view build_stamp;
        std::wstring_view runtime_context_summary;
        AbilityProbeDiagnosticState& diagnostic_state;
        RC::Unreal::UObject* last_world_context_object{nullptr};
    };

    auto make_runtime_ability_state_probe_deps(
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view build_stamp,
            std::wstring_view runtime_context_summary,
            RuntimeState& runtime_state) -> RuntimeAbilityStateProbeDeps;

    auto append_ability_state_probe_jsonl(
            const RuntimeAbilityStateProbeDeps& deps,
            std::wstring_view kind,
            std::wstring_view reason,
            std::wstring_view object_name,
            std::wstring_view class_name,
            std::wstring_view property_name,
            std::wstring_view property_class,
            int offset,
            int element_size,
            std::wstring_view value) -> void;

    auto log_ability_state_snapshot(
            const RuntimeAbilityStateProbeDeps& deps,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object) -> void;
}
