#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/vbonus/vbonus_actor_helpers.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Transform.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>
#include <chrono>
#include <format>
#include <string>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_activation::ActivationCallSummary;

    auto spawn_runtime_player_start(const SavePointSpawnRequest& request, UObject* world_context,
                                    const SpawnVector& location,
                                    RuntimePlayerStartSpawnSummary* summary, double yaw) -> UObject*
    {
        if (!summary)
        {
            return nullptr;
        }

        summary->requested = request.spawn_runtime_player_start;
        if (!request.spawn_runtime_player_start)
        {
            summary->status = STR("skipped");
            summary->reason = STR("not_requested");
            return nullptr;
        }

        summary->tag = choose_runtime_player_start_tag(request);
        if (!world_context || !kh3ap::is_uobject_valid_fast(world_context))
        {
            summary->status = STR("failed");
            summary->reason = STR("invalid_world_context");
            return nullptr;
        }

        UClass* const actor_class =
            try_resolve_runtime_player_start_class(&summary->class_resolution_source);
        if (!actor_class || !kh3ap::is_uobject_valid_fast(actor_class))
        {
            summary->status = STR("failed");
            summary->reason = STR("missing_player_start_class");
            return nullptr;
        }

        constexpr double degrees_to_half_radians = 3.14159265358979323846 / 360.0;
        const double half_yaw_radians = yaw * degrees_to_half_radians;
        const FTransform spawn_transform{
            FQuat{0.0, 0.0, std::sin(half_yaw_radians), std::cos(half_yaw_radians)},
            FVector{location.x, location.y, location.z},
            FVector{1.0, 1.0, 1.0},
        };

        AActor* spawned_actor = nullptr;
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            spawned_actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
                world_context, actor_class, spawn_transform,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
        }
        if (!spawned_actor || !kh3ap::is_uobject_valid_fast(spawned_actor))
        {
            summary->status = STR("failed");
            summary->reason = STR("typed_begin_spawn_returned_null");
            return nullptr;
        }

        summary->player_start_property = try_write_player_start_tag(spawned_actor, summary->tag);
        append_runtime_player_start_metadata_writes(summary, spawned_actor, request, summary->tag,
                                                    STR("deferred"));

        AActor* finished_actor = nullptr;
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            finished_actor = UGameplayStatics::FinishSpawningActor(spawned_actor, spawn_transform);
        }
        if (finished_actor && kh3ap::is_uobject_valid_fast(finished_actor))
        {
            spawned_actor = finished_actor;
        }

        summary->player_start_property = try_write_player_start_tag(spawned_actor, summary->tag);
        append_runtime_player_start_metadata_writes(summary, spawned_actor, request, summary->tag,
                                                    STR("finished"));
        prepare_runtime_player_start_actor(summary, spawned_actor);
        summary->actor = format_object_debug_name(spawned_actor);
        summary->status = STR("spawned");
        return spawned_actor;
    }

    auto spawn_save_point(const RuntimeSavePointSpawnDeps& deps,
                          const SavePointSpawnRequest& request, UObject* pawn,
                          const SpawnVector& location, std::wstring* class_resolution_source,
                          std::wstring* player_start_property,
                          std::array<ComponentAssignmentSummary, 3>* component_assignments,
                          SavePointSetupSummary* setup_summary, std::wstring* failure_reason)
        -> UObject*
    {
        const auto started_at = std::chrono::steady_clock::now();
        UClass* const actor_class = try_resolve_save_point_class(deps, class_resolution_source);
        const auto class_resolved_at = std::chrono::steady_clock::now();
        if (!actor_class || !pawn)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(STR("missing_prereq class={} pawn={}"),
                                              format_object_debug_name(actor_class),
                                              format_object_debug_name(pawn));
            }
            return nullptr;
        }

        const FTransform spawn_transform{
            FQuat{0.0, 0.0, 0.0, 1.0},
            FVector{location.x, location.y, location.z},
            FVector{1.0, 1.0, 1.0},
        };
        AActor* spawned_actor = nullptr;
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            spawned_actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
                pawn, actor_class, spawn_transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
                nullptr);
        }
        const auto begin_spawn_completed_at = std::chrono::steady_clock::now();
        if (!spawned_actor || !kh3ap::is_uobject_valid_fast(spawned_actor))
        {
            if (failure_reason)
            {
                *failure_reason = STR("typed_begin_spawn_returned_null");
            }
            return nullptr;
        }

        if (player_start_property)
        {
            *player_start_property =
                try_write_player_start_tag(spawned_actor, request.player_start_tag);
        }
        append_save_point_metadata_writes(setup_summary, spawned_actor, request, STR("deferred"));
        const auto deferred_metadata_completed_at = std::chrono::steady_clock::now();
        if (component_assignments)
        {
            *component_assignments = ensure_save_point_component_properties(spawned_actor, request);
        }
        const auto deferred_components_completed_at = std::chrono::steady_clock::now();

        AActor* finished_actor = nullptr;
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            finished_actor = UGameplayStatics::FinishSpawningActor(spawned_actor, spawn_transform);
        }
        const auto finish_spawn_completed_at = std::chrono::steady_clock::now();
        if (finished_actor && kh3ap::is_uobject_valid_fast(finished_actor))
        {
            spawned_actor = finished_actor;
        }

        if (player_start_property)
        {
            *player_start_property =
                try_write_player_start_tag(spawned_actor, request.player_start_tag);
        }
        append_save_point_metadata_writes(setup_summary, spawned_actor, request, STR("finished"));
        const auto finished_metadata_completed_at = std::chrono::steady_clock::now();
        // The component references were resolved before FinishSpawningActor
        // and remain owned by this actor. Re-validating all three here was
        // a duplicate global object scan costing roughly 200 ms per spawn.
        const auto finished_components_completed_at = std::chrono::steady_clock::now();
        activate_save_point_actor(setup_summary, spawned_actor, request);
        const auto activation_completed_at = std::chrono::steady_clock::now();
        const auto elapsed_us = [](auto begin, auto end) -> uint64_t
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
        };
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] RuntimeSavePointSpawnStages total_us={} resolve_class_us={} "
                "begin_spawn_us={} deferred_metadata_us={} deferred_components_us={} "
                "finish_spawn_us={} finished_metadata_us={} finished_components_us={} "
                "activation_us={} activation_calls={}\n"),
            elapsed_us(started_at, activation_completed_at),
            elapsed_us(started_at, class_resolved_at),
            elapsed_us(class_resolved_at, begin_spawn_completed_at),
            elapsed_us(begin_spawn_completed_at, deferred_metadata_completed_at),
            elapsed_us(deferred_metadata_completed_at, deferred_components_completed_at),
            elapsed_us(deferred_components_completed_at, finish_spawn_completed_at),
            elapsed_us(finish_spawn_completed_at, finished_metadata_completed_at),
            elapsed_us(finished_metadata_completed_at, finished_components_completed_at),
            elapsed_us(finished_components_completed_at, activation_completed_at),
            setup_summary ? setup_summary->activation_calls.size() : 0);
        if (deps.debug_logging && setup_summary)
        {
            for (const ActivationCallSummary& call : setup_summary->activation_calls)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeSavePointActivationCall elapsed_us={} status={} "
                        "function={} params={} values={}\n"),
                    call.elapsed_us, call.status, call.function_path,
                    call.param_name.empty() ? std::wstring{STR("<none>")} : call.param_name,
                    call.param_value.empty() ? std::wstring{STR("<none>")} : call.param_value);
            }
        }
        return spawned_actor;
    }
} // namespace kh3ap::save_point_detail
