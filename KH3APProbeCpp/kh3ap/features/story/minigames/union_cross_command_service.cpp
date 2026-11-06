#include "kh3ap/features/story/minigames/union_cross_command_service.h"

#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/story/minigames/union_cross_completion.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/core/events/wake_hook.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        enum class UnionCrossCompletionDispatchResult
        {
            Dispatched,
            PreparationFailed,
            DispatchThrew,
        };

        [[nodiscard]] auto classify_object(UObject* object)
                -> RuntimeUnionCrossObjectKind
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return RuntimeUnionCrossObjectKind::NotTarget;
            }
            UClass* const object_class = object->GetClassPrivate();
            if (!object_class)
            {
                return RuntimeUnionCrossObjectKind::NotTarget;
            }
            return classify_runtime_union_cross_object(
                    object_class->GetName(),
                    object->GetFullName());
        }

        [[nodiscard]] auto is_union_cross_level_script(UObject* object) -> bool
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return false;
            }
            UClass* const object_class = object->GetClassPrivate();
            return object_class &&
                   object_class->GetName() ==
                           runtime_union_cross_level_script_class_name &&
                   object->GetFullName().find(
                           runtime_union_cross_level_package_fragment) !=
                           std::wstring::npos;
        }

        [[nodiscard]] auto actor_identity(UObject* actor) noexcept -> uint64_t
        {
            return static_cast<uint64_t>(
                    reinterpret_cast<uintptr_t>(actor));
        }

        [[nodiscard]] auto try_read_started(
                UObject* actor,
                FBoolProperty* property) -> std::optional<bool>
        {
            if (!actor || !property)
            {
                return std::nullopt;
            }
            return property->GetPropertyValueInContainer(actor);
        }

    }

    struct RuntimeUnionCrossCommandService::Impl
    {
        explicit Impl(RuntimeUnionCrossCommandConfig value)
            : config(std::move(value))
        {
        }

        ~Impl() = default;

        auto resolve_notify_end_ride_function(
                UObject* actor,
                UObject* level_script) -> UFunction*
        {
            if (!actor || !level_script)
            {
                return nullptr;
            }

            const std::wstring derived_name =
                    std::wstring{STR("BndEvt__")} + actor->GetName() +
                    std::wstring{runtime_union_cross_notify_end_ride_function_suffix};
            if (UFunction* const derived =
                        level_script->GetFunctionByNameInChain(derived_name.c_str()))
            {
                return derived;
            }

            // The placed actor is serialized as g_kg_unioncross_ride_44 in the
            // stock level, and that suffix is baked into the compiled bound-
            // event function name. The live-actor classifier itself remains
            // suffix agnostic; this is only a fallback if UE renamed the live
            // instance after loading the level.
            return level_script->GetFunctionByNameInChain(
                    runtime_union_cross_baked_notify_end_ride_function_name.data());
        }

        auto dispatch_final_completion(UObject* actor)
                -> UnionCrossCompletionDispatchResult
        {
            if (classify_object(actor) != RuntimeUnionCrossObjectKind::LiveInstance)
            {
                return UnionCrossCompletionDispatchResult::PreparationFailed;
            }

            UObject* const persistent_level = actor->GetOuterPrivate();
            UObject* const level_script =
                    persistent_level && kh3ap::is_uobject_valid_fast(persistent_level) &&
                            persistent_level->GetName() == STR("PersistentLevel")
                    ? try_read_object_property_object(
                              persistent_level,
                              STR("LevelScriptActor"))
                    : nullptr;
            UFunction* const end_ride = actor->GetFunctionByNameInChain(
                    runtime_union_cross_end_ride_function_name.data());
            UFunction* const notify_end_ride =
                    is_union_cross_level_script(level_script)
                    ? resolve_notify_end_ride_function(actor, level_script)
                    : nullptr;
            FProperty* const union_cross_mode = notify_end_ride
                    ? find_function_param_by_name(
                              notify_end_ride,
                              STR("UnionCrossMode"))
                    : nullptr;

            std::wstring rejection_reason{};
            if (!persistent_level || !kh3ap::is_uobject_valid_fast(persistent_level))
            {
                rejection_reason = STR("persistent_level_unavailable");
            }
            else if (!is_union_cross_level_script(level_script))
            {
                rejection_reason = STR("level_script_unavailable");
            }
            else if (!end_ride || end_ride->GetParmsSize() != 0)
            {
                rejection_reason = STR("end_ride_signature_mismatch");
            }
            else if (!notify_end_ride)
            {
                rejection_reason = STR("notify_end_ride_unavailable");
            }
            else if (!union_cross_mode ||
                     union_cross_mode->GetClass().GetName() != STR("ByteProperty") ||
                     union_cross_mode->GetElementSize() != 1)
            {
                rejection_reason = STR("union_cross_mode_signature_mismatch");
            }

            const int32_t notify_params_size = notify_end_ride
                    ? notify_end_ride->GetParmsSize()
                    : 0;
            std::vector<std::byte> notify_params(
                    notify_params_size > 0
                            ? static_cast<size_t>(notify_params_size)
                            : 0);
            if (rejection_reason.empty() &&
                (notify_params.empty() ||
                 !write_integral_function_param(
                         union_cross_mode,
                         notify_params.data(),
                         runtime_union_cross_final_mode)))
            {
                rejection_reason = STR("union_cross_mode_param_write_failed");
            }

            if (!rejection_reason.empty())
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] UnionCrossImmediate completion rejected "
                            "reason={} actor={} persistent_level={} level_script={} "
                            "end_ride={} notify_end_ride={} notify_signature={}\n"),
                        rejection_reason,
                        truncate_for_log(format_object_debug_name(actor), 260),
                        truncate_for_log(
                                format_object_debug_name(persistent_level),
                                220),
                        truncate_for_log(
                                format_object_debug_name(level_script),
                                260),
                        truncate_for_log(format_object_debug_name(end_ride), 220),
                        truncate_for_log(
                                format_object_debug_name(notify_end_ride),
                                300),
                        notify_end_ride
                                ? function_param_signature(notify_end_ride)
                                : std::wstring{STR("<none>")});
                return UnionCrossCompletionDispatchResult::PreparationFailed;
            }

            try
            {
                // EndRide owns the ride actor's normal cleanup but does not
                // broadcast NotifyEndRide. Dispatch the level Blueprint's
                // baked bound-event wrapper with Phase3/final mode afterward;
                // that follows the stock final-success branch without loading
                // or redistributing the vanilla asset.
                actor->ProcessEvent(end_ride, nullptr);
                level_script->ProcessEvent(
                        notify_end_ride,
                        notify_params.data());
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] UnionCrossImmediate completion threw "
                            "actor={} level_script={}\n"),
                        truncate_for_log(format_object_debug_name(actor), 260),
                        truncate_for_log(
                                format_object_debug_name(level_script),
                                260));
                return UnionCrossCompletionDispatchResult::DispatchThrew;
            }

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] UnionCrossImmediate completion dispatched "
                        "trigger=ride_started final_mode={} actor={} level_script={} callback={}\n"),
                    runtime_union_cross_final_mode,
                    truncate_for_log(format_object_debug_name(actor), 260),
                    truncate_for_log(format_object_debug_name(level_script), 260),
                    truncate_for_log(
                            format_object_debug_name(notify_end_ride),
                            300));
            return UnionCrossCompletionDispatchResult::Dispatched;
        }

        auto tick() -> void
        {
            if(!config.enabled)return;
            if(!hook_attempted) {
                hook_attempted=true;
                hook_ready=start_hook.install(L"/Script/TresGame.TresGimmickKG_UnionCrossRide:StartUnionCross",16,work.notifier());
                Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] UnionCross start event hook installed={}\n",hook_ready);
            }
            const auto now=std::chrono::steady_clock::now();
            if(!hook_ready||!work.take(now))return;
            auto* const actor=active_actor.Get();
            if (!actor ||
                start_tracker.completion_dispatched)
            {
                return;
            }

            if(actor->HasAnyFlags(static_cast<EObjectFlags>(RF_NeedLoad|RF_NeedPostLoad|RF_NeedPostLoadSubobjects))) {
                work.finish(now,false,runtime_union_cross_retry_interval);return;
            }
            try
            {
                if (!kh3ap::is_uobject_valid_fast(actor) ||
                    actor->GetClassPrivate() != active_actor_class.Get())
                {
                    reset();
                    return;
                }

                const auto started =
                        try_read_started(actor, started_property);
                if (!started.has_value())
                {
                    if (!reflection_failure_reported)
                    {
                        reflection_failure_reported = true;
                        Output::send<LogLevel::Error>(
                                STR("[KH3APProbeCpp] UnionCrossImmediate start flag "
                                    "unavailable actor={}\n"),
                                truncate_for_log(
                                        format_object_debug_name(actor),
                                        260));
                    }
                    work.finish(now,false,runtime_union_cross_retry_interval);
                    return;
                }
                reflection_failure_reported = false;

                const RuntimeUnionCrossStartAction action =
                        start_tracker.observe_started(
                                actor_identity(actor),
                                *started);
                if (action != RuntimeUnionCrossStartAction::Complete)
                {
                    return;
                }

                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] UnionCrossImmediate ride started "
                            "actor={}\n"),
                        truncate_for_log(format_object_debug_name(actor), 260));
                if (dispatch_final_completion(actor) ==
                    UnionCrossCompletionDispatchResult::PreparationFailed)
                {
                    // Resolution failures happen before cleanup is dispatched,
                    // so retry while the ride remains started.
                    start_tracker.rearm_after_failed_completion();
                    work.finish(now,false,runtime_union_cross_retry_interval);
                }
            }
            catch (...)
            {
                work.finish(now,false,runtime_union_cross_retry_interval);
                if (!reflection_failure_reported)
                {
                    reflection_failure_reported = true;
                    Output::send<LogLevel::Error>(
                            STR("[KH3APProbeCpp] UnionCrossImmediate start poll "
                                "threw actor={}\n"),
                            truncate_for_log(
                                    format_object_debug_name(actor),
                                    260));
                }
            }
        }

        auto reset() -> void
        {
            active_actor = nullptr;
            active_actor_class = nullptr;
            started_property = nullptr;
            work.request();
            start_tracker.reset();
            reflection_failure_reported = false;
        }

        auto process_constructed_object(UObject* object) -> void
        {
            if (!config.enabled)
            {
                return;
            }
            if (config.enabled_for_current_seed &&
                !config.enabled_for_current_seed())
            {
                return;
            }
            const RuntimeUnionCrossObjectKind kind = classify_object(object);
            if (kind == RuntimeUnionCrossObjectKind::NotTarget)
            {
                return;
            }

            if (kind == RuntimeUnionCrossObjectKind::LiveInstance &&
                active_actor.Get() != object)
            {
                FProperty* const resolved_started =
                        object->GetPropertyByNameInChain(
                                runtime_union_cross_started_property_name.data());
                const bool started_valid =
                        resolved_started &&
                        resolved_started->GetClass().GetName() == STR("BoolProperty");
                if (!started_valid)
                {
                    Output::send<LogLevel::Error>(
                            STR("[KH3APProbeCpp] UnionCrossImmediate start cache "
                                "rejected started_found={} started_valid={} actor={}\n"),
                            resolved_started != nullptr,
                            started_valid,
                            truncate_for_log(
                                    format_object_debug_name(object),
                                    260));
                    return;
                }

                active_actor = make_uobject_weak(object);
                active_actor_class = make_uobject_weak(object->GetClassPrivate());
                if(active_actor.Get()!=object||active_actor_class.Get()!=object->GetClassPrivate())return;
                started_property =
                        static_cast<FBoolProperty*>(resolved_started);
                work.request();
                start_tracker.observe_actor(actor_identity(object));
                reflection_failure_reported = false;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] UnionCrossImmediate actor tracked "
                            "mode=native_start_event property={} "
                            "object={}\n"),
                        runtime_union_cross_started_property_name,
                        truncate_for_log(format_object_debug_name(object), 260));
            }
        }

        RuntimeUnionCrossCommandConfig config;
        RuntimeUnionCrossStartTracker start_tracker{};
        FWeakObjectPtr active_actor{},active_actor_class{};
        FBoolProperty* started_property{};
        RuntimeEventWork work;
        RuntimeNativeWakeHook start_hook;
        bool hook_attempted{},hook_ready{};
        bool reflection_failure_reported{};
    };

    RuntimeUnionCrossCommandService::RuntimeUnionCrossCommandService(
            RuntimeUnionCrossCommandConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeUnionCrossCommandService::~RuntimeUnionCrossCommandService() = default;
    RuntimeUnionCrossCommandService::RuntimeUnionCrossCommandService(
            RuntimeUnionCrossCommandService&&) noexcept = default;
    auto RuntimeUnionCrossCommandService::operator=(
            RuntimeUnionCrossCommandService&&) noexcept
            -> RuntimeUnionCrossCommandService& = default;

    auto RuntimeUnionCrossCommandService::process_constructed_object(
            UObject* object) -> void
    {
        impl_->process_constructed_object(object);
    }

    auto RuntimeUnionCrossCommandService::tick() -> void
    {
        impl_->tick();
    }

    auto RuntimeUnionCrossCommandService::reset() -> void
    {
        impl_->reset();
    }
}
