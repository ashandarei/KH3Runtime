#include <array>
#include <chrono>
#include <memory>
#include "kh3ap/features/player/emblems/lucky_emblem_tracker_service.h"
#include "kh3ap/features/player/emblems/lucky_emblem_persistence.h"

#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/checks/lucky_marks.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UActorComponent.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto max_lucky_tracker_parameter_bytes = 256;

        constexpr auto max_lucky_emblem_actor_candidates = 256;
        constexpr int maximum_detector_components = 16;
        constexpr int native_marker_vector_bytes = 12;

        constexpr auto max_emblem_query_parameter_bytes = 256;

        constexpr int32_t first_lucky_emblem_id = 1;
        constexpr int32_t last_lucky_emblem_id = 90;
        constexpr auto unfinished_component_flags =
            static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad |
                                      RF_NeedPostLoad | RF_NeedPostLoadSubobjects);

        // Streamed levels retain their package World as Outer. OwningWorld
        // identifies the actual playable world, including the rowboat's level.
        [[nodiscard]] auto actor_world(UObject* actor) -> UObject*
        {
            if (!is_uobject_a_fast(actor, AActor::StaticClass()))
            {
                return nullptr;
            }
            auto* level = actor->GetOuterPrivate();
            if (!is_uobject_valid_fast(level))
            {
                return nullptr;
            }
            auto* world = try_read_reflected_object_property_value(level, L"OwningWorld");
            return is_uobject_valid_fast(world) ? world : nullptr;
        }

        [[nodiscard]] auto parse_lucky_emblem_id(std::wstring_view value) -> std::optional<int32_t>
        {
            if (value.empty())
            {
                return std::nullopt;
            }

            int32_t parsed = 0;
            for (const wchar_t character : value)
            {
                if (character < L'0' || character > L'9')
                {
                    return std::nullopt;
                }
                parsed = parsed * 10 + static_cast<int32_t>(character - L'0');
                if (parsed > last_lucky_emblem_id)
                {
                    return std::nullopt;
                }
            }
            return parsed >= first_lucky_emblem_id ? std::optional<int32_t>{parsed} : std::nullopt;
        }

        [[nodiscard]] auto owned_emblem_components(AActor* actor, UClass* component_class)
            -> std::vector<UObject*>
        {
            if (!is_uobject_a_fast(actor, AActor::StaticClass()) ||
                !is_uclass_child_of_fast(component_class, UActorComponent::StaticClass()))
            {
                return {};
            }
            // The UE4SS actor wrapper retains a raw UFunction. Resolve this
            // inherited function on the live owner for each discovery instead.
            auto* function = actor->GetFunctionByNameInChain(L"GetComponentsByClass");
            if (!is_uobject_a_fast(function, UFunction::StaticClass()))
            {
                return {};
            }
            const auto size = function->GetParmsSize();
            auto* result = find_function_param_by_name(function, L"ReturnValue");
            auto* requested_class = find_function_param_by_name(function, L"ComponentClass");
            if (size <= 0 || size > max_emblem_query_parameter_bytes || !result ||
                !requested_class || requested_class->GetClass().GetName() != L"ClassProperty" ||
                requested_class->GetElementSize() != sizeof(UClass*) ||
                requested_class->GetOffset_Internal() < 0 ||
                requested_class->GetOffset_Internal() + requested_class->GetElementSize() > size ||
                result->GetClass().GetName() != L"ArrayProperty" ||
                result->GetOffset_Internal() < 0 ||
                result->GetOffset_Internal() + result->GetElementSize() > size)
            {
                return {};
            }
            auto* inner = static_cast<FArrayProperty*>(result)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return {};
            }
            struct Array
            {
                UObject** data;
                int32_t count, capacity;
            };
            if (result->GetElementSize() != sizeof(Array))
            {
                return {};
            }
            std::vector<std::byte> params(size);
            function->InitializeStruct(params.data());
            std::vector<UObject*> components;
            try
            {
                *requested_class->ContainerPtrToValuePtr<UClass*>(params.data()) = component_class;
                actor->ProcessEvent(function, params.data());
                const auto* owned = result->ContainerPtrToValuePtr<Array>(params.data());
                if (owned->data && owned->count > 0 &&
                    owned->count <= maximum_detector_components && owned->capacity >= owned->count)
                {
                    components.assign(owned->data, owned->data + owned->count);
                }
            }
            catch (...)
            {
                components.clear();
            }
            function->DestroyStruct(params.data());
            return components;
        }

        [[nodiscard]] auto query_lucky_emblem_recorded(UObject* component,
                                                       std::wstring_view function_name)
            -> std::optional<bool>
        {
            if (!component || !is_uobject_valid_fast(component))
            {
                return std::nullopt;
            }

            UFunction* const function = component->GetFunctionByNameInChain(function_name.data());
            if (!is_uobject_a_fast(function, UFunction::StaticClass()))
            {
                return std::nullopt;
            }
            FProperty* const return_property =
                function ? find_function_param_by_name(function, STR("ReturnValue")) : nullptr;
            const bool valid_return =
                return_property && return_property->GetClass().GetName() == STR("BoolProperty");
            const int32_t params_size = function ? function->GetParmsSize() : 0;
            if (!function || !valid_return || params_size <= 0)
            {
                return std::nullopt;
            }

            std::vector<std::byte> params(static_cast<size_t>(params_size));
            function->InitializeStruct(params.data());
            try
            {
                component->ProcessEvent(function, params.data());
                const bool recorded = static_cast<FBoolProperty*>(return_property)
                                          ->GetPropertyValueInContainer(params.data());
                function->DestroyStruct(params.data());
                return recorded;
            }
            catch (...)
            {
                function->DestroyStruct(params.data());
                return std::nullopt;
            }
        }
    } // namespace

    struct RuntimeLuckyEmblemTrackerService::Impl
    {
        struct TrackedComponent
        {
            FWeakObjectPtr component{};
            int32_t mark_id{};
            std::optional<bool> last_recorded{};
            bool identity_failure_logged{};
            bool query_failure_logged{};
        };

        explicit Impl(RuntimeLuckyEmblemTrackerConfig value) : config(std::move(value))
        {
        }

        auto resolve_bridge_dir() const -> std::optional<std::filesystem::path>
        {
            return config.bridge_dir_resolver ? config.bridge_dir_resolver() : std::nullopt;
        }

        auto reset_for_bridge(const std::filesystem::path& bridge_dir) -> void
        {
            active_bridge_dir = bridge_dir.lexically_normal();
            captured_ids.clear();
            latest_capture_id.reset();
            sequence = 0;
            dirty = true;
            persistence_failed = false;
            for (TrackedComponent& tracked : components)
            {
                tracked.last_recorded.reset();
            }
        }

        auto write_state(const std::filesystem::path& bridge_dir) -> bool
        {
            std::ostringstream json;
            json << "{\n";
            json << "  \"schema\": \"kh3ap.runtime_lucky_emblem_state.v1\",\n";
            json << "  \"build_stamp\": \"" << json_escape(config.build_stamp) << "\",\n";
            json << "  \"sequence\": " << sequence << ",\n";
            json << "  \"captured_global_ids\": [";
            size_t index = 0;
            for (const int32_t mark_id : captured_ids)
            {
                if (index++ > 0)
                {
                    json << ", ";
                }
                json << mark_id;
            }
            json << "],\n";
            json << "  \"latest_capture_global_id\": ";
            if (latest_capture_id.has_value())
            {
                json << *latest_capture_id;
            }
            else
            {
                json << "null";
            }
            json << ",\n";
            json << "  \"observed_at_utc\": \"" << now_iso8601_utc() << "\",\n";
            const std::wstring context = config.runtime_context_resolver
                                             ? config.runtime_context_resolver()
                                             : std::wstring{};
            json << "  \"runtime_context\": \"" << json_escape(context) << "\"\n";
            json << "}\n";

            return write_text_file_atomic_verified(
                bridge_dir / std::filesystem::path{std::wstring{config.state_filename}},
                json.str());
        }

        auto process_constructed_object(UObject* object) -> void
        {
            if (!config.enabled || !object || !is_uobject_valid_fast(object))
            {
                return;
            }
            const auto duplicate = std::find_if(components.begin(), components.end(),
                                                [object](const TrackedComponent& tracked)
                                                {
                                                    return tracked.component.Get() == object;
                                                });
            if (duplicate != components.end())
            {
                return;
            }

            TrackedComponent tracked{};
            tracked.component = make_uobject_weak(object);
            if (tracked.component.Get() != object)
            {
                return;
            }
            components.push_back(std::move(tracked));
            work.request();
        }

        auto resolve_mark_id(TrackedComponent& tracked, UObject* component) -> bool
        {
            if (tracked.mark_id >= first_lucky_emblem_id && tracked.mark_id <= last_lucky_emblem_id)
            {
                return true;
            }

            try
            {
                const auto reflected_id =
                    try_read_object_name_property(component, config.mark_id_property_name.data());
                const auto parsed_id =
                    reflected_id ? parse_lucky_emblem_id(*reflected_id) : std::nullopt;
                if (parsed_id.has_value())
                {
                    tracked.mark_id = *parsed_id;
                    tracked.identity_failure_logged = false;
                    return true;
                }
                if (reflected_id && !reflected_id->empty() && *reflected_id != L"None")
                {
                    // Installed content can contain non-retail IDs (e.g. 91).
                    // A finished, unsupported identity is not pending loading.
                    tracked.mark_id = -1;
                    return false;
                }
            }
            catch (...)
            {
            }

            if (!tracked.identity_failure_logged)
            {
                tracked.identity_failure_logged = true;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] LuckyEmblemTracker waiting for numeric {} property\n"),
                    config.mark_id_property_name);
            }
            return false;
        }

        auto reconcile_components(UObject* world_context) -> void
        {
            auto* component_class =
                find_loaded_runtime_asset_class(L"/Script/TresGame.TresHiddenMickeyComponent");
            if (!is_uclass_child_of_fast(component_class, UActorComponent::StaticClass()))
            {
                return;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeHotPathProfileScope profile{L"map_overlay:emblem_discovery"};
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const auto before = components.size();
            size_t owned_components = 0;
#endif
            std::set<AActor*> seen;
            // The native detector actor variants are already resident with
            // their map. Do not load assets or scan the global UObject array.
            for (const auto* path :
                 {L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_01.g_ex_luckymark_01_C",
                  L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_02.g_ex_luckymark_02_C",
                  L"/Game/Blueprints/Gimmick/ex/g_ex_luckymark_03.g_ex_luckymark_03_C"})
            {
                // Resolve resident classes through the engine's soft-class
                // reference, avoiding the raw global cache after travel/GC.
                auto* actor_class = find_loaded_runtime_asset_class(path);
                const auto actors = find_live_actors_of_class(world_context, actor_class,
                                                              max_lucky_emblem_actor_candidates);
                for (auto* actor : actors)
                {
                    if (!is_uobject_valid_fast(actor) ||
                        actor->HasAnyFlags(unfinished_component_flags) ||
                        !seen.insert(actor).second)
                    {
                        continue;
                    }
                    // Enumerate the owning actor's components, not the boat's
                    // visual meshes or the emblem's attachment parent.
                    const auto owned = owned_emblem_components(actor, component_class);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    owned_components += owned.size();
#endif
                    for (auto* component : owned)
                    {
                        if (is_uobject_a_fast(component, component_class) &&
                            !component->HasAnyFlags(unfinished_component_flags))
                        {
                            process_constructed_object(component);
                        }
                    }
                }
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Area map emblem discovery actors={} owned_components={} "
                    "added={} tracked={}\n"),
                seen.size(), owned_components, components.size() - before, components.size());
#endif
        }

        auto poll_components(bool photo_finished) -> bool
        {
            bool complete = true;
            for (auto iterator = components.begin(); iterator != components.end();)
            {
                UObject* const component = iterator->component.Get();
                if (!component || !is_uobject_valid_fast(component))
                {
                    iterator = components.erase(iterator);
                    continue;
                }

                // Templates never finish loading into gameplay detectors.
                // Constructor notifications include these objects as well.
                if (component->HasAnyFlags(
                        static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)))
                {
                    iterator = components.erase(iterator);
                    continue;
                }

                if (component->HasAnyFlags(unfinished_component_flags))
                {
                    complete = false;
                    ++iterator;
                    continue;
                }

                if (!resolve_mark_id(*iterator, component))
                {
                    if (iterator->mark_id < 0)
                    {
                        iterator = components.erase(iterator);
                        continue;
                    }
                    complete = false;
                    ++iterator;
                    continue;
                }

                const auto recorded =
                    query_lucky_emblem_recorded(component, config.recorded_function_name);
                if (!recorded.has_value())
                {
                    complete = false;
                    if (!iterator->query_failure_logged)
                    {
                        iterator->query_failure_logged = true;
                        Output::send<LogLevel::Error>(
                            STR("[KH3APProbeCpp] LuckyEmblemTracker query unavailable mark_id={} "
                                "function={}\n"),
                            iterator->mark_id, config.recorded_function_name);
                    }
                    ++iterator;
                    continue;
                }
                iterator->query_failure_logged = false;

                const bool captured_transition = photo_finished &&
                                                 iterator->last_recorded.has_value() &&
                                                 !*iterator->last_recorded && *recorded;
                iterator->last_recorded = *recorded;
                const bool newly_captured =
                    *recorded && captured_ids.insert(iterator->mark_id).second;
                if (captured_transition)
                {
                    latest_capture_id = iterator->mark_id;
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] LuckyEmblemTracker captured mark_id={}\n"),
                        iterator->mark_id);
                }
                if (newly_captured || captured_transition)
                {
                    ++sequence;
                    dirty = true;
                }
                ++iterator;
            }
            return complete;
        }

        auto unrecorded_markers(UObject* world_context, bool rediscover,
                                std::vector<RuntimeLuckyEmblemMarker>& markers) -> void
        {
            markers.clear();
            if (!config.enabled)
            {
                return;
            }
            auto* world = actor_world(world_context);
            if (!world)
            {
                return;
            }
            if (rediscover)
            {
                reconcile_components(world_context);
            }
            std::set<int32_t> seen;
            for (auto& tracked : components)
            {
                auto* component = tracked.component.Get();
                if (!is_uobject_valid_fast(component) ||
                    component->HasAnyFlags(unfinished_component_flags) ||
                    actor_world(component->GetOuterPrivate()) != world ||
                    !resolve_mark_id(tracked, component) || seen.contains(tracked.mark_id))
                {
                    continue;
                }
                const auto recorded =
                    query_lucky_emblem_recorded(component, config.recorded_function_name);
                if (!recorded.has_value() || *recorded)
                {
                    continue;
                }

                // Emblems can be attached far from their actor's origin. Read
                // the detector's world transform, never its relative position.
                auto* function = component->GetFunctionByNameInChain(L"K2_GetComponentLocation");
                if (!is_uobject_valid_fast(function) || function->GetParmsSize() <= 0 ||
                    function->GetParmsSize() > max_emblem_query_parameter_bytes)
                {
                    continue;
                }
                auto* result = find_function_param_by_name(function, L"ReturnValue");
                if (!result || result->GetClass().GetName() != L"StructProperty" ||
                    result->GetElementSize() != native_marker_vector_bytes ||
                    result->GetOffset_Internal() < 0 ||
                    result->GetOffset_Internal() + native_marker_vector_bytes >
                        function->GetParmsSize())
                {
                    continue;
                }

                std::array<std::byte, max_lucky_tracker_parameter_bytes> params{};
                component->ProcessEvent(function, params.data());
                RuntimeLuckyEmblemMarker marker{tracked.mark_id};
                std::memcpy(marker.location.data(),
                            result->ContainerPtrToValuePtr<void>(params.data()),
                            native_marker_vector_bytes);
                if (!std::all_of(marker.location.begin(), marker.location.end(),
                                 [](float value)
                                 {
                                     return std::isfinite(value);
                                 }))
                {
                    continue;
                }
                seen.insert(tracked.mark_id);
                markers.push_back(marker);
            }
            return;
        }

        auto tick(std::chrono::steady_clock::time_point now) -> void
        {
            if (!config.enabled)
            {
                return;
            }
            if (!photo_events_attempted)
            {
                photo_events_attempted = true;
                try
                {
                    persistence.install_observer();
                }
                catch (const std::exception& error)
                {
                    const std::string reason = error.what();
                    Output::send<LogLevel::Error>(
                        L"[KH3APProbeCpp] Lucky Emblem photo observer unavailable: {}\n",
                        std::wstring(reason.begin(), reason.end()));
                }
            }
            const auto epoch = reconciliation::epoch();
            if (epoch != observed_epoch)
            {
                observed_epoch = epoch;
                persistence_failed = false;
                work.request();
            }
            const auto generation = LuckyEmblemPersistence::photo_revision();
            if (generation != observed_photo_generation)
            {
                observed_photo_generation = generation;
                photo_pending = true;
                work.request();
            }
            const auto reward_revision = lucky_marks::revision();
            if (reward_revision != observed_reward_revision)
            {
                observed_reward_revision = reward_revision;
                persistence_failed = false;
                work.request();
            }
            if (!work.take(now))
            {
                return;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RuntimeHotPathProfileScope profile{L"lucky_emblems:event_work"};
#endif

            const auto bridge_dir = resolve_bridge_dir();
            if (!bridge_dir.has_value())
            {
                work.finish(now, false, config.retry_interval);
                return;
            }
            const std::filesystem::path normalized = bridge_dir->lexically_normal();
            if (!active_bridge_dir.has_value() || *active_bridge_dir != normalized)
            {
                reset_for_bridge(normalized);
            }

            bool persistence_ready = persistence_failed;
            if (!persistence_failed)
            {
                try
                {
                    const auto binding = reconciliation::view();
                    auto* pawn =
                        config.playable_pawn_resolver ? config.playable_pawn_resolver() : nullptr;
                    persistence_ready =
                        binding->ready &&
                        persistence.reconcile(normalized, binding->seed, binding->slot, pawn);
                }
                catch (const std::exception& error)
                {
                    persistence_failed = true;
                    persistence_ready = true;
                    const std::string reason = error.what();
                    Output::send<LogLevel::Error>(
                        L"[KH3APProbeCpp] LuckyEmblemPersistence stopped reason={}\n",
                        std::wstring(reason.begin(), reason.end()));
                }
            }
            const bool complete = poll_components(photo_pending);
            if (complete)
            {
                photo_pending = false;
            }
            if (dirty && write_state(normalized))
            {
                dirty = false;
            }
            work.finish(now, complete && !dirty && persistence_ready, config.retry_interval);
        }

        auto reset_map_lifecycle() -> void
        {
            components.clear();
            persistence_failed = false;
            work.request();
        }

        RuntimeLuckyEmblemTrackerConfig config;
        LuckyEmblemPersistence persistence;
        bool persistence_failed{};
        bool photo_pending{};
        std::vector<TrackedComponent> components{};
        std::set<int32_t> captured_ids{};
        std::optional<int32_t> latest_capture_id{};
        std::optional<std::filesystem::path> active_bridge_dir{};
        RuntimeEventWork work;
        uint64_t observed_photo_generation{};
        uint64_t observed_reward_revision{};
        bool photo_events_attempted{};
        int64_t observed_epoch{-1};
        uint64_t sequence{};
        bool dirty{};
    };

    RuntimeLuckyEmblemTrackerService::RuntimeLuckyEmblemTrackerService(
        RuntimeLuckyEmblemTrackerConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeLuckyEmblemTrackerService::~RuntimeLuckyEmblemTrackerService() = default;
    RuntimeLuckyEmblemTrackerService::RuntimeLuckyEmblemTrackerService(
        RuntimeLuckyEmblemTrackerService&&) noexcept = default;
    auto RuntimeLuckyEmblemTrackerService::operator=(RuntimeLuckyEmblemTrackerService&&) noexcept
        -> RuntimeLuckyEmblemTrackerService& = default;

    auto RuntimeLuckyEmblemTrackerService::process_constructed_object(UObject* object) -> void
    {
        impl_->process_constructed_object(object);
    }

    auto RuntimeLuckyEmblemTrackerService::tick(std::chrono::steady_clock::time_point now) -> void
    {
        impl_->tick(now);
    }

    auto RuntimeLuckyEmblemTrackerService::reset_map_lifecycle() -> void
    {
        impl_->reset_map_lifecycle();
    }

    auto RuntimeLuckyEmblemTrackerService::notify_context_ready() -> void
    {
        impl_->persistence_failed = false;
        impl_->work.request();
    }

    auto RuntimeLuckyEmblemTrackerService::unrecorded_markers(
        UObject* world_context, bool rediscover, std::vector<RuntimeLuckyEmblemMarker>& markers)
        -> void
    {
        impl_->unrecorded_markers(world_context, rediscover, markers);
    }
} // namespace kh3ap
