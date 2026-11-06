#include "kh3ap/features/story/minigames/beat_of_lead_service.h"

#include "kh3ap/features/story/minigames/beat_of_lead_completion.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/core/events/wake_hook.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <chrono>
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
        enum class BeatOfLeadCompletionDispatchResult
        {
            Dispatched,
            PreparationFailed,
            DispatchThrew,
        };

        [[nodiscard]] auto classify_object(UObject* object)
                -> RuntimeBeatOfLeadObjectKind
        {
            if (!object)
            {
                return RuntimeBeatOfLeadObjectKind::NotTarget;
            }
            UClass* const object_class = object->GetClassPrivate();
            if (!object_class)
            {
                return RuntimeBeatOfLeadObjectKind::NotTarget;
            }
            return classify_runtime_beat_of_lead_object(
                    object_class->GetName(),
                    object->GetFullName());
        }

        [[nodiscard]] auto object_name_without_scan(UObject* object)
                -> std::wstring
        {
            if (!object)
            {
                return STR("<null>");
            }
            try
            {
                const std::wstring full_name = object->GetFullName();
                return full_name.empty() ? std::wstring{STR("<unnamed>")} : full_name;
            }
            catch (...)
            {
                return STR("<name-unavailable>");
            }
        }

        [[nodiscard]] auto manager_identity(UObject* manager) noexcept -> uint64_t
        {
            return static_cast<uint64_t>(
                    reinterpret_cast<uintptr_t>(manager));
        }

        [[nodiscard]] auto try_read_score(
                UObject* manager,
                FProperty* property) -> std::optional<int32_t>
        {
            if (!manager || !property)
            {
                return std::nullopt;
            }
            const int32_t* const value =
                    property->ContainerPtrToValuePtr<int32_t>(manager);
            if (!value)
            {
                return std::nullopt;
            }
            return *value;
        }

        [[nodiscard]] auto is_story_gameflow_level_script(UObject* object)
                -> bool
        {
            if (!object)
            {
                return false;
            }
            UClass* const object_class = object->GetClassPrivate();
            return object_class &&
                   object_class->GetName() ==
                           runtime_beat_of_lead_level_script_class_name &&
                   object->GetFullName().find(
                           runtime_beat_of_lead_level_script_package_fragment) !=
                           std::wstring::npos;
        }
    }

    struct RuntimeBeatOfLeadService::Impl
    {
        explicit Impl(RuntimeBeatOfLeadConfig value)
            : config(std::move(value))
        {
        }

        auto dispatch_story_completion(UObject* manager)
                -> BeatOfLeadCompletionDispatchResult
        {
            if (classify_object(manager) !=
                RuntimeBeatOfLeadObjectKind::LiveInstance)
            {
                return BeatOfLeadCompletionDispatchResult::PreparationFailed;
            }

            UObject* const persistent_level = manager->GetOuterPrivate();
            UObject* const level_script =
                    persistent_level &&
                            persistent_level->GetName() == STR("PersistentLevel")
                    ? try_read_object_property_object(
                              persistent_level,
                              STR("LevelScriptActor"))
                    : nullptr;
            UFunction* const on_wipe_out =
                    is_story_gameflow_level_script(level_script)
                    ? level_script->GetFunctionByNameInChain(
                              runtime_beat_of_lead_wipe_out_function_name.data())
                    : nullptr;
            FProperty* const group_name = on_wipe_out
                    ? find_function_param_by_name(
                              on_wipe_out,
                              runtime_beat_of_lead_wipe_out_group_param_name)
                    : nullptr;
            FProperty* const enemy = on_wipe_out
                    ? find_function_param_by_name(
                              on_wipe_out,
                              runtime_beat_of_lead_wipe_out_enemy_param_name)
                    : nullptr;
            FProperty* const killed_by = on_wipe_out
                    ? find_function_param_by_name(
                              on_wipe_out,
                              runtime_beat_of_lead_wipe_out_killed_by_param_name)
                    : nullptr;
            const bool group_name_valid =
                    group_name &&
                    group_name->GetClass().GetName() == STR("NameProperty") &&
                    group_name->GetElementSize() ==
                            static_cast<int32_t>(sizeof(FName));
            const bool enemy_valid =
                    enemy && enemy->GetClass().GetName() == STR("ObjectProperty");
            const bool killed_by_valid =
                    killed_by &&
                    killed_by->GetClass().GetName() == STR("ObjectProperty");
            const int32_t params_size =
                    on_wipe_out ? on_wipe_out->GetParmsSize() : 0;
            std::vector<std::byte> params(
                    params_size > 0 ? static_cast<size_t>(params_size) : 0);
            FName* const group_name_value =
                    group_name_valid && !params.empty()
                    ? group_name->ContainerPtrToValuePtr<FName>(params.data())
                    : nullptr;

            if (!persistent_level || !is_story_gameflow_level_script(level_script) ||
                !on_wipe_out || !group_name_valid || !enemy_valid ||
                !killed_by_valid || params.empty() || !group_name_value)
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] BeatOfLeadFast completion rejected "
                            "reason=story_wipe_out_route_unavailable "
                            "persistent_level={} level_script={} on_wipe_out={} "
                            "group_name_valid={} enemy_valid={} "
                            "killed_by_valid={} params_size={} signature={} "
                            "manager={}\n"),
                        object_name_without_scan(persistent_level),
                        object_name_without_scan(level_script),
                        object_name_without_scan(on_wipe_out),
                        group_name_valid,
                        enemy_valid,
                        killed_by_valid,
                        params_size,
                        on_wipe_out
                                ? function_param_signature(on_wipe_out)
                                : std::wstring{STR("<none>")},
                        truncate_for_log(object_name_without_scan(manager), 260));
                return BeatOfLeadCompletionDispatchResult::PreparationFailed;
            }

            *group_name_value = FName(
                    runtime_beat_of_lead_story_wipe_out_group,
                    FNAME_Add);

            try
            {
                // Stock story completion enters through OnWipeOut("hyper").
                // That Blueprint branch broadcasts MissionEnd (which calls the
                // manager's GameEnd) and then continues into the normal result
                // and story-transition flow. The object parameters are unused
                // by this compiled branch and remain null in the zeroed buffer.
                level_script->ProcessEvent(on_wipe_out, params.data());
            }
            catch (...)
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] BeatOfLeadFast completion threw "
                            "manager={}\n"),
                        truncate_for_log(object_name_without_scan(manager), 260));
                return BeatOfLeadCompletionDispatchResult::DispatchThrew;
            }

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BeatOfLeadFast completion dispatched "
                        "trigger=first_scored_kill "
                        "route=gameflow.OnWipeOut group={} "
                        "stock_mission_end_and_result_flow=true story_only=true "
                        "no_object_scans=true manager={} level_script={}\n"),
                    runtime_beat_of_lead_story_wipe_out_group,
                    truncate_for_log(object_name_without_scan(manager), 260),
                    truncate_for_log(object_name_without_scan(level_script), 260));
            return BeatOfLeadCompletionDispatchResult::Dispatched;
        }

        auto tick() -> void
        {
            if(!config.enabled)return;
            if(!hooks_attempted) {
                hooks_attempted=true;
                hooks_ready=score_hook.install(L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_SetGigasGameGetScore",4,work.notifier()) &&
                    total_hook.install(L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_SetGigasGameTotalScore",4,work.notifier());
                Output::send<LogLevel::Normal>(L"[KH3APProbeCpp] BeatOfLead score event hooks installed={}\n",hooks_ready);
            }
            const auto now=std::chrono::steady_clock::now();
            if(!hooks_ready||!work.take(now))return;
            if (!config.enabled || completion_tracker.completion_dispatched)
            {
                return;
            }

            UObject* const manager = active_manager.Get();
            if (!manager)
            {
                if (completion_tracker.manager_identity != 0)
                {
                    reset();
                }
                return;
            }

            if(manager->HasAnyFlags(static_cast<EObjectFlags>(RF_NeedLoad|RF_NeedPostLoad|RF_NeedPostLoadSubobjects))) {
                work.finish(now,false,runtime_beat_of_lead_retry_interval);return;
            }

            try
            {
                const bool repeatable_minigame =
                        minigame_property->GetPropertyValueInContainer(manager);
                const auto score = try_read_score(manager, score_property);
                if (!score.has_value())
                {
                    if (!reflection_failure_reported)
                    {
                        reflection_failure_reported = true;
                        Output::send<LogLevel::Error>(
                                STR("[KH3APProbeCpp] BeatOfLeadFast score "
                                    "unavailable manager={}\n"),
                                truncate_for_log(
                                        object_name_without_scan(manager),
                                        260));
                    }
                    work.finish(now,false,runtime_beat_of_lead_retry_interval);
                    return;
                }
                reflection_failure_reported = false;

                const RuntimeBeatOfLeadCompletionAction action =
                        completion_tracker.observe_state(
                                manager_identity(manager),
                                repeatable_minigame,
                                *score);
                if (action != RuntimeBeatOfLeadCompletionAction::Complete)
                {
                    return;
                }

                if (dispatch_story_completion(manager) ==
                    BeatOfLeadCompletionDispatchResult::PreparationFailed)
                {
                    completion_tracker.rearm_after_failed_completion();
                    work.finish(now,false,runtime_beat_of_lead_retry_interval);
                }
            }
            catch (...)
            {
                work.finish(now,false,runtime_beat_of_lead_retry_interval);
                if (!reflection_failure_reported)
                {
                    reflection_failure_reported = true;
                    Output::send<LogLevel::Error>(
                            STR("[KH3APProbeCpp] BeatOfLeadFast state poll "
                                "threw manager={}\n"),
                            truncate_for_log(
                                    object_name_without_scan(manager),
                                    260));
                }
            }
        }

        auto reset() -> void
        {
            active_manager = nullptr;
            minigame_property = nullptr;
            score_property = nullptr;
            work.request();
            completion_tracker.reset();
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
            if (classify_object(object) !=
                RuntimeBeatOfLeadObjectKind::LiveInstance ||
                active_manager.Get() == object)
            {
                return;
            }

            FProperty* const resolved_minigame =
                    object->GetPropertyByNameInChain(
                            runtime_beat_of_lead_minigame_property_name.data());
            FProperty* const resolved_score =
                    object->GetPropertyByNameInChain(
                            runtime_beat_of_lead_score_property_name.data());
            const bool minigame_valid =
                    resolved_minigame &&
                    resolved_minigame->GetClass().GetName() == STR("BoolProperty");
            const bool score_valid =
                    resolved_score &&
                    resolved_score->GetClass().GetName() == STR("IntProperty") &&
                    resolved_score->GetElementSize() ==
                            static_cast<int32_t>(sizeof(int32_t));
            if (!minigame_valid || !score_valid)
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] BeatOfLeadFast manager cache rejected "
                            "minigame_found={} minigame_valid={} score_found={} "
                            "score_valid={} manager={}\n"),
                        resolved_minigame != nullptr,
                        minigame_valid,
                        resolved_score != nullptr,
                        score_valid,
                        truncate_for_log(object_name_without_scan(object), 260));
                return;
            }

            active_manager = make_uobject_weak(object);
            if(active_manager.Get()!=object)return;
            minigame_property =
                    static_cast<FBoolProperty*>(resolved_minigame);
            score_property = resolved_score;
            work.request();
            completion_tracker.observe_manager(manager_identity(object));
            reflection_failure_reported = false;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BeatOfLeadFast manager tracked "
                        "mode=native_score_events "
                        "score_property={} repeatable_property={} "
                        "no_object_scans=true manager={}\n"),
                    runtime_beat_of_lead_score_property_name,
                    runtime_beat_of_lead_minigame_property_name,
                    truncate_for_log(object_name_without_scan(object), 260));
        }

        RuntimeBeatOfLeadConfig config;
        RuntimeBeatOfLeadCompletionTracker completion_tracker{};
        FWeakObjectPtr active_manager{};
        FBoolProperty* minigame_property{};
        FProperty* score_property{};
        RuntimeEventWork work;
        RuntimeNativeWakeHook score_hook,total_hook;
        bool hooks_attempted{},hooks_ready{};
        bool reflection_failure_reported{};
    };

    RuntimeBeatOfLeadService::RuntimeBeatOfLeadService(
            RuntimeBeatOfLeadConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeBeatOfLeadService::~RuntimeBeatOfLeadService() = default;
    RuntimeBeatOfLeadService::RuntimeBeatOfLeadService(
            RuntimeBeatOfLeadService&&) noexcept = default;
    auto RuntimeBeatOfLeadService::operator=(
            RuntimeBeatOfLeadService&&) noexcept
            -> RuntimeBeatOfLeadService& = default;

    auto RuntimeBeatOfLeadService::process_constructed_object(
            UObject* object) -> void
    {
        impl_->process_constructed_object(object);
    }

    auto RuntimeBeatOfLeadService::tick() -> void
    {
        impl_->tick();
    }

    auto RuntimeBeatOfLeadService::reset() -> void
    {
        impl_->reset();
    }
}
