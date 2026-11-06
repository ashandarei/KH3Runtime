#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    using ManualAbilityStateProbeEmitter = void (*)(
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object);

    struct ManualAbilityStateProbeDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view protocol_name;
        int64_t protocol_version;
        std::wstring_view runtime_context_summary;
        std::wstring_view build_stamp;
        ManualAbilityStateProbeEmitter ability_array_needle_probe_emitter;
        ManualAbilityStateProbeEmitter targeted_sora_state_probe_emitter;
        ManualAbilityStateProbeEmitter live_ability_owner_probe_emitter;
    };

    auto make_manual_ability_state_probe_deps(
            RuntimeState& runtime_state,
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view request_filename,
            std::wstring_view result_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view runtime_context_summary,
            std::wstring_view build_stamp,
            ManualAbilityStateProbeEmitter ability_array_needle_probe_emitter,
            ManualAbilityStateProbeEmitter targeted_sora_state_probe_emitter,
            ManualAbilityStateProbeEmitter live_ability_owner_probe_emitter) -> ManualAbilityStateProbeDeps;

    auto process_pending_manual_ability_state_probe(const ManualAbilityStateProbeDeps& deps) -> void;
}
