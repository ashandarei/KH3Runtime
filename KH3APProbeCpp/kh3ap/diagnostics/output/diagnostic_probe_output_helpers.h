#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class AbilityProbeDiagnosticState;
    class RuntimeBlockingUiState;
    class RuntimeState;
    class StableAbilityTraceDedupe;
    enum class HookPhase;
    struct AbilityArrayNeedleProbeEntry;
    struct GfxProbeFunctionPaths;
    struct HookTargetInfo;
    struct PropertyProbeEntry;
    struct StableAbilityTraceData;
    struct RuntimeBlockingUiProbeUpdateData;
}

namespace RC::Unreal
{
    struct FFrame;
    class UObject;
    class UFunction;
}

namespace kh3ap
{

    struct RuntimeDiagnosticProbeOutputDeps
    {
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view build_stamp;
        std::wstring_view runtime_context_summary;
        AbilityProbeDiagnosticState* diagnostic_state{nullptr};
    };

    auto make_runtime_diagnostic_probe_output_deps(
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view build_stamp,
            std::wstring_view runtime_context_summary = {},
            AbilityProbeDiagnosticState* diagnostic_state = nullptr) -> RuntimeDiagnosticProbeOutputDeps;

    auto make_runtime_ability_diagnostic_probe_output_deps(
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view build_stamp,
            std::wstring_view runtime_context_summary,
            RuntimeState& runtime_state) -> RuntimeDiagnosticProbeOutputDeps;

    auto log_runtime_update_heartbeat_if_needed(
            RuntimeState& runtime_state,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration check_interval,
            std::chrono::steady_clock::duration log_interval) -> void;

    auto append_property_probe_jsonl(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            const std::filesystem::path& filename,
            const PropertyProbeEntry& entry) -> void;

    auto append_ability_array_needle_probe_jsonl(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            const AbilityArrayNeedleProbeEntry& entry,
            std::wstring_view reason) -> void;

    auto append_ability_array_needle_probe_data(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> void;

    auto log_ability_array_needle_probe_if_enabled(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            AbilityProbeDiagnosticState& diagnostic_state,
            bool probe_enabled,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> void;

    auto log_ability_array_needle_probe_if_enabled(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            RuntimeState& runtime_state,
            bool probe_enabled,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object) -> void;

    auto append_targeted_sora_state_probe_data(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            RuntimeState& runtime_state,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object) -> void;

    auto log_targeted_sora_state_probe_data(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            RuntimeState& runtime_state,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object) -> void;

    auto append_live_ability_owner_probe_data(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> void;

    auto log_live_ability_owner_probe_if_enabled(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            AbilityProbeDiagnosticState& diagnostic_state,
            bool probe_enabled,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> void;

    auto log_live_ability_owner_probe_if_enabled(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            RuntimeState& runtime_state,
            bool probe_enabled,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object) -> void;

    auto log_targeted_chrinit_probe_data(
            bool probe_enabled,
            bool thunder_patch_enabled,
            std::wstring_view reason,
            std::wstring_view runtime_context) -> void;

    auto log_ability_discovery_snapshot_data(
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object,
            std::wstring_view runtime_context) -> void;

    auto log_ability_discovery_snapshot_data(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object,
            RC::Unreal::UObject* last_world_context_object) -> void;

    auto log_ability_discovery_snapshot_data(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            RuntimeState& runtime_state,
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object) -> void;

    auto log_stable_ability_trace_data(
            StableAbilityTraceDedupe& dedupe,
            const StableAbilityTraceData& trace_data) -> void;

    auto log_stable_ability_trace_if_enabled(
            StableAbilityTraceDedupe& dedupe,
            const HookTargetInfo& hook_info,
            HookPhase phase,
            RC::Unreal::FFrame& frame,
            RC::Unreal::UFunction* target_function,
            void* params,
            std::wstring_view context_name) -> void;

    auto log_stable_ability_trace_if_enabled(
            RuntimeState& runtime_state,
            const HookTargetInfo& hook_info,
            HookPhase phase,
            RC::Unreal::FFrame& frame,
            RC::Unreal::UFunction* target_function,
            void* params,
            std::wstring_view context_name) -> void;

    auto write_runtime_blocking_ui_state_file(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            bool active,
            std::wstring_view reason,
            std::wstring_view ui_context,
            std::wstring_view event_name,
            int64_t callback_id) -> void;

    auto update_runtime_blocking_ui_state_from_probe_data(
            const RuntimeDiagnosticProbeOutputDeps& deps,
            RuntimeBlockingUiState& blocking_ui_state,
            const RuntimeBlockingUiProbeUpdateData& update_data) -> void;
}
