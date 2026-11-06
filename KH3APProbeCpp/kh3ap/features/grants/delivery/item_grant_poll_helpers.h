#pragma once

#include "kh3ap/features/grants/delivery/item_grant_apply_helpers.h"
#include "kh3ap/features/grants/abilities/targeted_ability_query_helpers.h"
#include "kh3ap/core/types.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    using AbilityStateSnapshotEmitter = void (*)(
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object);

    using RuntimeLiveAbilityOwnerProbeEmitter = void (*)(
            std::wstring_view reason,
            RC::Unreal::UObject* preferred_object);

    struct RuntimeItemGrantPostApplyObserverDeps
    {
        RuntimeState& runtime_state;
        bool runtime_ability_state_probe_enabled;
        bool targeted_runtime_ability_query_probe_enabled;
        std::wstring_view runtime_context_summary;
        std::array<std::pair<std::wstring_view, std::wstring_view>, 3> targeted_ability_query_functions;
        AbilityStateSnapshotEmitter ability_snapshot_emitter;
        AbilityProbeJsonlEmitter targeted_ability_probe_jsonl_emitter;
        RuntimeLiveAbilityOwnerProbeEmitter live_ability_owner_probe_emitter;
    };

    auto make_runtime_item_grant_post_apply_observer_deps(
            RuntimeState& runtime_state,
            bool runtime_ability_state_probe_enabled,
            bool targeted_runtime_ability_query_probe_enabled,
            std::wstring_view runtime_context_summary,
            std::array<std::pair<std::wstring_view, std::wstring_view>, 3> targeted_ability_query_functions,
            AbilityStateSnapshotEmitter ability_snapshot_emitter,
            AbilityProbeJsonlEmitter targeted_ability_probe_jsonl_emitter,
            RuntimeLiveAbilityOwnerProbeEmitter live_ability_owner_probe_emitter) -> RuntimeItemGrantPostApplyObserverDeps;

    struct RuntimeItemGrantPollDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view runtime_item_grant_filename;
        std::wstring_view runtime_item_grant_priority_filename;
        std::wstring_view runtime_item_apply_filename;
        std::wstring_view protocol_name;
        int64_t protocol_version;
        std::wstring_view runtime_context_summary;
        std::chrono::steady_clock::duration runtime_item_grant_delay;
        std::chrono::steady_clock::duration slow_wait_log_delay;
        bool runtime_context_object_not_real;
        RuntimeItemGrantApplyDeps apply_deps;
        RuntimeItemGrantPostApplyObserverDeps post_apply_observer_deps;
    };

    auto make_runtime_item_grant_poll_deps(
            RuntimeState& runtime_state,
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view runtime_item_grant_filename,
            std::wstring_view runtime_item_grant_priority_filename,
            std::wstring_view runtime_item_apply_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view runtime_context_summary,
            std::chrono::steady_clock::duration runtime_item_grant_delay,
            std::chrono::steady_clock::duration slow_wait_log_delay,
            bool runtime_context_object_not_real,
            RuntimeItemGrantApplyDeps apply_deps,
            RuntimeItemGrantPostApplyObserverDeps post_apply_observer_deps) -> RuntimeItemGrantPollDeps;

    auto process_pending_runtime_item_grant(const RuntimeItemGrantPollDeps& deps) -> void;

    auto observe_runtime_item_grant_applied(
            const RuntimeItemGrantRequest& request,
            RC::Unreal::UObject* applied_world_context_object,
            bool is_batch,
            const RuntimeItemGrantPostApplyObserverDeps& deps) -> void;
}
