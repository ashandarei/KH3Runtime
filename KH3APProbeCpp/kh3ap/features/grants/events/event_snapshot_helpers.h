#pragma once

#include "kh3ap/core/types.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class FProperty;
    class UObject;
}

namespace kh3ap
{
    class DeferredEventStateResampleQueue;
    class EventArraySnapshotStore;
    class RuntimeState;

    using EventArrayRemovedEmitter = void (*)(std::wstring_view source, std::wstring_view snapshot_key, std::wstring_view entry);
    using EventStateSnapshotLogger = std::function<void(std::wstring_view source)>;

    struct RuntimeEventSnapshotDeps
    {
        EventArraySnapshotStore& snapshot_store;
        DeferredEventStateResampleQueue& deferred_resample_queue;
        EventArrayRemovedEmitter removed_emitter;
    };

    auto make_runtime_event_snapshot_deps(
            RuntimeState& runtime_state,
            EventArrayRemovedEmitter removed_emitter) -> RuntimeEventSnapshotDeps;

    auto log_event_array_diff_if_any(
            RC::Unreal::UObject* object,
            std::wstring_view source,
            std::wstring_view property_name,
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            EventArraySnapshotStore& snapshot_store) -> void;
    auto begin_event_state_snapshot_pass(EventArraySnapshotStore& snapshot_store) -> void;
    auto finalize_event_state_snapshot_pass(
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter) -> void;
    auto log_object_rewardish_properties(
            RC::Unreal::UObject* object,
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter,
            int depth = 0) -> void;
    auto log_object_rewardish_properties(
            RC::Unreal::UObject* object,
            std::wstring_view source,
            const RuntimeEventSnapshotDeps& deps,
            int depth = 0) -> void;
    auto accumulate_tracked_arrays_only(
            RC::Unreal::UObject* object,
            EventArraySnapshotStore& snapshot_store) -> void;
    auto log_found_object_class_snapshot(
            std::wstring_view class_name,
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter,
            int max_objects = 2) -> void;
    auto log_randomizer_event_state_snapshot(
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter) -> void;
    auto log_randomizer_event_state_snapshot(
            std::wstring_view source,
            const RuntimeEventSnapshotDeps& deps) -> void;
    auto enqueue_deferred_event_state_resample(
            std::wstring_view source_prefix,
            uint64_t delay_seconds,
            DeferredEventStateResampleQueue& queue) -> void;
    auto enqueue_deferred_event_state_resample(
            std::wstring_view source_prefix,
            uint64_t delay_seconds,
            const RuntimeEventSnapshotDeps& deps) -> void;
    auto drain_due_deferred_event_state_resamples(
            DeferredEventStateResampleQueue& queue,
            EventStateSnapshotLogger snapshot_logger) -> void;
    auto drain_due_deferred_event_state_resamples(const RuntimeEventSnapshotDeps& deps) -> void;
    auto log_vbonus_level_actor_snapshot(
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter) -> std::vector<VBonusLevelActorSnapshotEntry>;
    auto log_vbonus_level_actor_snapshot(
            std::wstring_view source,
            const RuntimeEventSnapshotDeps& deps) -> std::vector<VBonusLevelActorSnapshotEntry>;
}
