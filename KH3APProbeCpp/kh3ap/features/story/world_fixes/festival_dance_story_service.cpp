#include "kh3ap/features/story/world_fixes/festival_dance_story_service.h"

#include "kh3ap/features/story/world_fixes/festival_dance_story_patch.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    struct RuntimeFestivalDanceStoryService::Impl
    {
        explicit Impl(RuntimeFestivalDanceStoryConfig value)
            : config(std::move(value))
        {
        }

        auto finish_if_complete() -> void
        {
            if (!progress.complete())
            {
                return;
            }

            if (config.disable_object_observation)
            {
                config.disable_object_observation();
            }
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] FestivalDanceStoryFast ready story_only=true "
                        "RapunzelDanceTotalTime=4 FlynnSplineStayTime=2 "
                        "trigger=uobject_create polling=false observer_disabled=true\n"));
        }

        auto apply_fast_patch(UObject* object) -> void
        {
            if (!config.enabled || progress.complete())
            {
                return;
            }

            const auto patch_result =
                    try_patch_runtime_festival_dance_story_object(object);
            if (patch_result.object_kind ==
                RuntimeFestivalDanceStoryObjectKind::NotTarget)
            {
                return;
            }

            const bool already_applied = progress.contains(patch_result.object_kind);
            if (patch_result.success)
            {
                progress.record_success(patch_result.object_kind);
                if (!already_applied)
                {
                    const auto& wave1 = patch_result.writes[0];
                    const auto& rapunzel = patch_result.writes[1];
                    const auto& mission = patch_result.writes[2];
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] FestivalDanceStoryFast applied kind={} object={} "
                                "Wave1ClearValue={}->{}({}) WaveRaDanceFinishValue={}->{}({}) "
                                "MissionClearValue={}->{}({})\n"),
                            runtime_festival_dance_story_object_kind_name(
                                    patch_result.object_kind),
                            truncate_for_log(patch_result.object_name, 220),
                            wave1.original_value,
                            wave1.requested_value,
                            runtime_festival_dance_story_property_write_status_name(
                                    wave1.status),
                            rapunzel.original_value,
                            rapunzel.requested_value,
                            runtime_festival_dance_story_property_write_status_name(
                                    rapunzel.status),
                            mission.original_value,
                            mission.requested_value,
                            runtime_festival_dance_story_property_write_status_name(
                                    mission.status));
                }
            }
            else if (!patch_error_logged)
            {
                patch_error_logged = true;
                const auto& wave1 = patch_result.writes[0];
                const auto& rapunzel = patch_result.writes[1];
                const auto& mission = patch_result.writes[2];
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] FestivalDanceStoryFast rejected kind={} object={} "
                            "Wave1ClearValue={}({}) WaveRaDanceFinishValue={}({}) "
                            "MissionClearValue={}({})\n"),
                        runtime_festival_dance_story_object_kind_name(
                                patch_result.object_kind),
                        truncate_for_log(patch_result.object_name, 220),
                        wave1.original_value,
                        runtime_festival_dance_story_property_write_status_name(
                                wave1.status),
                        rapunzel.original_value,
                        runtime_festival_dance_story_property_write_status_name(
                                rapunzel.status),
                        mission.original_value,
                        runtime_festival_dance_story_property_write_status_name(
                                mission.status));
            }

            finish_if_complete();
        }

        auto apply_timing_patch(UObject* object) -> void
        {
            if (!config.enabled ||
                progress.complete() ||
                progress.manager_timing_complete())
            {
                return;
            }

            const auto patch_result =
                    try_patch_runtime_festival_dance_story_timing_object(object);
            if (!patch_result.is_target)
            {
                return;
            }

            if (patch_result.success)
            {
                progress.record_manager_timing_success();
                const auto& rapunzel_total = patch_result.writes[0];
                const auto& flynn_stay = patch_result.writes[1];
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] FestivalDanceStoryTiming applied object={} "
                            "RapunzelDanceTotalTime={}->{}({}) "
                            "FlynnSplineStayTime={}->{}({})\n"),
                        truncate_for_log(patch_result.object_name, 220),
                        rapunzel_total.original_value,
                        rapunzel_total.requested_value,
                        runtime_festival_dance_story_property_write_status_name(
                                rapunzel_total.status),
                        flynn_stay.original_value,
                        flynn_stay.requested_value,
                        runtime_festival_dance_story_property_write_status_name(
                                flynn_stay.status));
            }
            else if (!timing_patch_error_logged)
            {
                timing_patch_error_logged = true;
                const auto& rapunzel_total = patch_result.writes[0];
                const auto& flynn_stay = patch_result.writes[1];
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] FestivalDanceStoryTiming rejected object={} "
                            "RapunzelDanceTotalTime={}({}) FlynnSplineStayTime={}({})\n"),
                        truncate_for_log(patch_result.object_name, 220),
                        rapunzel_total.original_value,
                        runtime_festival_dance_story_property_write_status_name(
                                rapunzel_total.status),
                        flynn_stay.original_value,
                        runtime_festival_dance_story_property_write_status_name(
                                flynn_stay.status));
            }

            finish_if_complete();
        }

        auto process_constructed_object(UObject* object) -> void
        {
            if (!config.enabled || !object || progress.complete())
            {
                return;
            }
            if (config.enabled_for_current_seed &&
                !config.enabled_for_current_seed())
            {
                return;
            }

            // The create notification gives us the exact target pointer. Reach the
            // story level script's CDO directly through UClass so there is never a
            // global UObject lookup or recurring search. The timing values are
            // deliberately written only to the live manager below, which keeps the
            // repeatable post-story minigame and future manager instances vanilla.
            UClass* const object_class = object->GetClassPrivate();
            if (object_class)
            {
                try
                {
                    UObject* const class_default_object =
                            object_class->GetClassDefaultObject();
                    if (class_default_object && class_default_object != object)
                    {
                        apply_fast_patch(class_default_object);
                    }
                }
                catch (...)
                {
                    // The observed object is still safe to patch. A later target
                    // creation can retry the direct CDO access without polling.
                }
            }
            apply_fast_patch(object);
            apply_timing_patch(object);
        }

        RuntimeFestivalDanceStoryConfig config;
        RuntimeFestivalDanceStoryPatchProgress progress{};
        bool patch_error_logged{false};
        bool timing_patch_error_logged{false};
    };

    RuntimeFestivalDanceStoryService::RuntimeFestivalDanceStoryService(
            RuntimeFestivalDanceStoryConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeFestivalDanceStoryService::~RuntimeFestivalDanceStoryService() = default;
    RuntimeFestivalDanceStoryService::RuntimeFestivalDanceStoryService(
            RuntimeFestivalDanceStoryService&&) noexcept = default;
    auto RuntimeFestivalDanceStoryService::operator=(
            RuntimeFestivalDanceStoryService&&) noexcept
            -> RuntimeFestivalDanceStoryService& = default;

    auto RuntimeFestivalDanceStoryService::process_constructed_object(
            UObject* object) -> void
    {
        impl_->process_constructed_object(object);
    }
}
