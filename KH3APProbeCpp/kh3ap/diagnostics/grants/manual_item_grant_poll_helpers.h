#pragma once

#include "kh3ap/features/grants/delivery/item_grant_apply_helpers.h"
#include "kh3ap/core/types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    using ManualRuntimeItemGrantProcessor = std::function<void(RC::Unreal::UObject* preferred_world_context_object)>;

    using ManualRuntimeLiveAbilityOwnerProbeEmitter = void (*)(
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object);

    struct ManualRuntimeItemGrantDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view manual_request_filename;
        std::wstring_view manual_result_filename;
        std::wstring_view protocol_name;
        int64_t protocol_version;
        RuntimeItemGrantApplyDeps apply_deps;
        ManualRuntimeLiveAbilityOwnerProbeEmitter live_ability_owner_probe_emitter;
        bool emit_pending_results;
    };

    auto make_manual_runtime_item_grant_deps(
            RuntimeState& runtime_state,
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view manual_request_filename,
            std::wstring_view manual_result_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            RuntimeItemGrantApplyDeps apply_deps,
            ManualRuntimeLiveAbilityOwnerProbeEmitter live_ability_owner_probe_emitter,
            bool emit_pending_results) -> ManualRuntimeItemGrantDeps;

    auto process_armed_manual_runtime_item_grant(
            RuntimeState& runtime_state,
            std::chrono::steady_clock::duration ready_delay,
            const ManualRuntimeItemGrantProcessor& processor) -> void;

    auto process_pending_manual_runtime_item_grant_fallback(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view request_filename,
            std::chrono::steady_clock::duration arm_grace,
            std::chrono::steady_clock::duration fallback_cooldown,
            std::wstring_view runtime_context_summary,
            const ManualRuntimeItemGrantProcessor& processor) -> void;

    auto process_pending_manual_runtime_item_grant(
            const ManualRuntimeItemGrantDeps& deps,
            RC::Unreal::UObject* preferred_world_context_object = nullptr) -> void;
}
