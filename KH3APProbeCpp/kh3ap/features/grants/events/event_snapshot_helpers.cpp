#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/grants/events/event_snapshot_helpers.h"

#include "kh3ap/diagnostics/deferred_event_state_resample_queue.h"
#include "kh3ap/features/grants/events/event_array_snapshot_store.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/diagnostics/reflection/reflection_diagnostics.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/pipe_transport.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <sstream>
#include <string>

namespace kh3ap
{
    namespace
    {
        constexpr auto event_snapshot_wait_log_interval = std::chrono::seconds(5);
        constexpr auto event_snapshot_log_limit = 24;
    }

    auto make_runtime_event_snapshot_deps(
            RuntimeState& runtime_state,
            EventArrayRemovedEmitter removed_emitter) -> RuntimeEventSnapshotDeps
    {
        return {
                runtime_state.event_state().event_array_snapshot_store(),
                runtime_state.event_state().deferred_resample_queue(),
                removed_emitter};
    }

    auto log_event_array_diff_if_any(
            RC::Unreal::UObject* object,
            std::wstring_view source,
            std::wstring_view property_name,
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            EventArraySnapshotStore& snapshot_store) -> void
    {
        (void)source;
        if (!object || !should_track_event_array_diffs(property_name))
        {
            return;
        }

        const auto signatures = try_collect_array_entry_diff_signatures(property, value_ptr, 4096);
        if (!signatures.has_value())
        {
            return;
        }

        RC::Unreal::UClass* const object_class = object->GetClassPrivate();
        const std::wstring class_name = object_class ? object_class->GetName() : std::wstring{STR("<unknown_class>")};
        const std::wstring snapshot_key = std::format(STR("{}|{}"), class_name, property_name);
        snapshot_store.accumulate(snapshot_key, *signatures);
    }

    auto begin_event_state_snapshot_pass(EventArraySnapshotStore& snapshot_store) -> void
    {
        snapshot_store.begin_pass();
    }

    auto finalize_event_state_snapshot_pass(
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter) -> void
    {
        const auto emissions = snapshot_store.finalize_pass();

        for (const auto& emission : emissions)
        {
            if (emission.warm_cache)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] {} | event_array_warm_cache key={} | seeded_count={}\n"),
                        source,
                        emission.snapshot_key,
                        static_cast<unsigned int>(emission.seeded_count));
                continue;
            }
            for (const auto& entry : emission.added)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] {} | event_array_added key={} | entry={}\n"),
                        source,
                        emission.snapshot_key,
                        entry);
            }
            for (const auto& entry : emission.removed)
            {
                if (entry.starts_with(STR("Vbonus_")))
                {
                    // Preserve the former log-reader fallback even when a
                    // token has no native bridge-key mapping yet.
                    const auto event = "{\"kind\":\"removed_vbonus\",\"source_id\":\"" +
                        json_escape(entry) + "\",\"source\":\"" + json_escape(source) +
                        "\",\"snapshot_key\":\"" + json_escape(emission.snapshot_key) + "\"}";
                    pipe::write(channels::runtime_fight_events, event);
                }
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] {} | event_array_removed key={} | entry={}\n"),
                        source,
                        emission.snapshot_key,
                        entry);
                if (removed_emitter)
                {
                    removed_emitter(source, emission.snapshot_key, entry);
                }
            }
        }
    }

    auto log_object_rewardish_properties(
            RC::Unreal::UObject* object,
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter,
            int depth) -> void
    {
        if (!object || depth > 1)
        {
            return;
        }

        RC::Unreal::UClass* object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return;
        }

        int logged_count = 0;
        for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
        {
            if (!property)
            {
                continue;
            }

            const std::wstring property_name = property->GetName();
            const std::wstring property_class = property->GetClass().GetName();
            if (!should_log_rewardish_property(property_name, property_class))
            {
                continue;
            }

            const int offset = property->GetOffset_Internal();
            const int element_size = property->GetElementSize();
            if (offset < 0 || element_size <= 0)
            {
                continue;
            }

            const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
            const void* value_ptr = object_bytes + offset;
            const uint64_t raw_u64 = read_unsigned_param_value(value_ptr, std::min(element_size, 8));

            std::wstring extra_value = STR("<none>");
            if (property_class == STR("NameProperty"))
            {
                const auto decoded = decode_name_value(raw_u64);
                const std::wstring& candidate = !decoded.fname_pair.empty() && decoded.fname_pair != STR("None")
                                                        ? decoded.fname_pair
                                                        : decoded.fname_first;
                if (!candidate.empty())
                {
                    extra_value = candidate;
                }
            }
            else if (property_class == STR("StrProperty"))
            {
                if (const auto text = try_read_object_string_property(object, property_name.c_str()); text.has_value())
                {
                    extra_value = *text;
                }
            }
            else if (property_class == STR("ObjectProperty"))
            {
                RC::Unreal::UObject* const object_value = *static_cast<RC::Unreal::UObject* const*>(const_cast<void*>(value_ptr));
                if (object_value)
                {
                    extra_value = object_value->GetFullName();
                }
            }
            else if (property_class == STR("ArrayProperty"))
            {
                ArrayHeader header{};
                std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
                std::wostringstream stream;
                stream << STR("num=") << header.num << STR(" max=") << header.max << STR(" ptr=") << header.data_ptr;
                if (const auto entries = try_format_supported_array_entries(property, value_ptr); entries.has_value())
                {
                    stream << STR(" ") << *entries;
                }
                extra_value = stream.str();
                log_event_array_diff_if_any(object, source, property_name, property, value_ptr, snapshot_store);
            }
            else if (property_class == STR("MapProperty"))
            {
                MapHeader header{};
                std::memcpy(&header, value_ptr, sizeof(MapHeader));
                std::wostringstream stream;
                stream << STR("num=") << header.num << STR(" max=") << header.max << STR(" ptr=") << header.data_ptr;
                if (const auto entries = try_format_supported_map_entries(property, value_ptr); entries.has_value())
                {
                    stream << STR(" entries=") << *entries;
                }
                extra_value = stream.str();
            }
            else if (property_class == STR("BoolProperty"))
            {
                extra_value = read_unsigned_param_value(value_ptr, std::min(element_size, 1)) != 0 ? STR("true") : STR("false");
            }
            else if (property_class == STR("StructProperty"))
            {
                extra_value = std::format(
                        STR("raw={}"),
                        format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(element_size, 1, 32))));
            }

            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] {} | object_property object={} | name={} | class={} | offset={} | elem_size={} | raw={} | value={}\n"),
                    source,
                    object->GetFullName(),
                    property_name,
                    property_class,
                    offset,
                    element_size,
                    format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(element_size, 1, 16))),
                    extra_value);

            if (property_class == STR("ObjectProperty"))
            {
                RC::Unreal::UObject* const object_value = *static_cast<RC::Unreal::UObject* const*>(const_cast<void*>(value_ptr));
                if (object_value)
                {
                    const std::wstring object_class_name = object_value->GetClassPrivate() ? object_value->GetClassPrivate()->GetName() : STR("");
                    if (object_class_name.find(STR("EventPersistanceData_C")) != std::wstring::npos)
                    {
                        log_object_rewardish_properties(
                                object_value,
                                std::format(STR("{} nested"), source),
                                snapshot_store,
                                removed_emitter,
                                depth + 1);
                    }
                }
            }

            ++logged_count;
            if (logged_count >= event_snapshot_log_limit)
            {
                break;
            }
        }
    }

    auto accumulate_tracked_arrays_only(
            RC::Unreal::UObject* object,
            EventArraySnapshotStore& snapshot_store) -> void
    {
        if (!object)
        {
            return;
        }

        RC::Unreal::UClass* object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return;
        }

        for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
        {
            if (!property)
            {
                continue;
            }

            const std::wstring property_class = property->GetClass().GetName();
            if (property_class != STR("ArrayProperty"))
            {
                continue;
            }

            const std::wstring property_name = property->GetName();
            if (!should_track_event_array_diffs(property_name))
            {
                continue;
            }

            const int offset = property->GetOffset_Internal();
            if (offset < 0)
            {
                continue;
            }

            const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
            const void* value_ptr = object_bytes + offset;
            log_event_array_diff_if_any(object, STR("accumulate_only"), property_name, property, value_ptr, snapshot_store);
        }
    }

    auto log_object_rewardish_properties(
            RC::Unreal::UObject* object,
            std::wstring_view source,
            const RuntimeEventSnapshotDeps& deps,
            int depth) -> void
    {
        log_object_rewardish_properties(object, source, deps.snapshot_store, deps.removed_emitter, depth);
    }

    auto log_found_object_class_snapshot(
            std::wstring_view class_name,
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter,
            int max_objects) -> void
    {
        std::vector<RC::Unreal::UObject*> objects{};
        RC::Unreal::UObjectGlobals::FindAllOf(class_name, objects);
        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] {} | {} count={}\n"),
                source,
                class_name,
                objects.size());

        int logged = 0;
        for (RC::Unreal::UObject* object : objects)
        {
            if (!object)
            {
                continue;
            }

            if (logged < max_objects)
            {
                log_object_rewardish_properties(
                        object,
                        std::format(STR("{} {}"), source, class_name),
                        snapshot_store,
                        removed_emitter);
                ++logged;
            }
            else
            {
                accumulate_tracked_arrays_only(object, snapshot_store);
            }
        }
    }

    auto log_randomizer_event_state_snapshot(
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter) -> void
    {
        begin_event_state_snapshot_pass(snapshot_store);
        log_found_object_class_snapshot(STR("EventActor_C"), source, snapshot_store, removed_emitter);
        log_found_object_class_snapshot(STR("EventPostActor_C"), source, snapshot_store, removed_emitter);
        log_found_object_class_snapshot(STR("EventPersistanceData_C"), source, snapshot_store, removed_emitter);
        finalize_event_state_snapshot_pass(source, snapshot_store, removed_emitter);
    }

    auto log_randomizer_event_state_snapshot(
            std::wstring_view source,
            const RuntimeEventSnapshotDeps& deps) -> void
    {
        log_randomizer_event_state_snapshot(source, deps.snapshot_store, deps.removed_emitter);
    }

    auto enqueue_deferred_event_state_resample(
            std::wstring_view source_prefix,
            uint64_t delay_seconds,
            DeferredEventStateResampleQueue& queue) -> void
    {
        const auto source_label = std::format(STR("{}_resample_delay{}"), std::wstring{source_prefix}, delay_seconds);
        const auto result = queue.schedule(
                source_label,
                std::chrono::seconds(delay_seconds),
                DeferredEventStateResampleQueue::max_pending);
        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Deferred resample queued source={} delay_seconds={} pending={}\n"),
                source_label,
                delay_seconds,
                static_cast<uint64_t>(result.pending_count));
    }

    auto enqueue_deferred_event_state_resample(
            std::wstring_view source_prefix,
            uint64_t delay_seconds,
            const RuntimeEventSnapshotDeps& deps) -> void
    {
        enqueue_deferred_event_state_resample(source_prefix, delay_seconds, deps.deferred_resample_queue);
    }

    auto drain_due_deferred_event_state_resamples(
            DeferredEventStateResampleQueue& queue,
            EventStateSnapshotLogger snapshot_logger) -> void
    {
        const auto now = std::chrono::steady_clock::now();
        const auto result = queue.pop_due(now);
        if (result.pending_count == 0)
        {
            return;
        }

        if (result.source_labels.empty())
        {
            if (queue.should_log_pending_wait(now, event_snapshot_wait_log_interval))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Deferred resample pending pending={} earliest_ms_remaining={}\n"),
                        static_cast<uint64_t>(result.pending_count),
                        result.earliest_ms_remaining.has_value()
                                ? *result.earliest_ms_remaining
                                : static_cast<int64_t>(-1));
            }
            return;
        }

        for (const auto& label : result.source_labels)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Deferred resample firing source={}\n"),
                    label);
            if (snapshot_logger)
            {
                snapshot_logger(label);
            }
        }
    }

    auto drain_due_deferred_event_state_resamples(const RuntimeEventSnapshotDeps& deps) -> void
    {
        drain_due_deferred_event_state_resamples(
                deps.deferred_resample_queue,
                [&deps](std::wstring_view source) {
                    log_randomizer_event_state_snapshot(source, deps);
                });
    }

    auto log_vbonus_level_actor_snapshot(
            std::wstring_view source,
            EventArraySnapshotStore& snapshot_store,
            EventArrayRemovedEmitter removed_emitter) -> std::vector<VBonusLevelActorSnapshotEntry>
    {
        const auto snapshot = collect_vbonus_level_actor_snapshot();
        if (snapshot.empty())
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] {} | VBonusLevelActor_C count=0\n"),
                    source);
            return snapshot;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] {} | VBonusLevelActor_C count={}\n"),
                source,
                snapshot.size());

        for (size_t index = 0; index < snapshot.size(); ++index)
        {
            const auto& entry = snapshot[index];
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] {} | VBonusLevelActor[{}]={} | LevelName={} | vbonusName={}\n"),
                    source,
                    index,
                    entry.full_name,
                    entry.level_name,
                    entry.vbonus_name);
        }

        std::vector<RC::Unreal::UObject*> actors{};
        RC::Unreal::UObjectGlobals::FindAllOf(STR("VBonusLevelActor_C"), actors);
        if (!actors.empty() && actors[0])
        {
            log_object_rewardish_properties(actors[0], source, snapshot_store, removed_emitter);
        }

        return snapshot;
    }

    auto log_vbonus_level_actor_snapshot(
            std::wstring_view source,
            const RuntimeEventSnapshotDeps& deps) -> std::vector<VBonusLevelActorSnapshotEntry>
    {
        return log_vbonus_level_actor_snapshot(source, deps.snapshot_store, deps.removed_emitter);
    }
}
