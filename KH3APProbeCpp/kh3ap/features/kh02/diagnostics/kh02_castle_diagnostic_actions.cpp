#include "kh3ap/features/player/player_action_commands.h"
#include "kh02_diagnostic_action_helpers.h"
#include "kh3ap/features/kh02/story/kh02_entry_session.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/story/kh02_gameplay_binding.h"
#include "kh3ap/features/kh02/story/kh02_castle_gameplay_session.h"
#include "kh3ap/features/kh02/story/kh02_map_session.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh3ap/features/kh02/story/kh02_timed_inputs.h"
#include "kh3ap/features/kh02/story/kh02_story_targets.h"
#include "kh3ap/features/kh02/core/kh02_mesh_buffer_lease.h"
#include "kh3ap/features/kh02/core/kh02_localization.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_ids.h"
#include "kh3ap/features/kh02/runtime/kh02_runtime_config.h"
#include "kh3ap/features/kh02/world/kh02_mapset.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_interaction_policy.h"
#include <limits>
#include "kh3ap/core/input/runtime_player_control.h"

namespace kh3ap
{
    using namespace kh02diagnostic_detail;

    namespace
    {
        constexpr int resume_mapset_request_base = 0x4b483600;
    }

    auto Kh02CastleDiagnosticActions::execute(UObject* pawn, UObject* w, Action action,
                                              Kh02CastleActionServices services) -> std::optional<std::string>
    {
        auto& entry = services.entry;
        auto& gameplay = services.gameplay;
        auto& castle = services.castle;
        auto& map_session = services.map_session;
        auto& challenge_log = services.challenge_log;
        auto& movie_bridge = services.movie_bridge;
        auto& source_flags = services.source_flags;
        auto& timed_inputs = services.timed_inputs;
        auto& within_challenges = services.within_challenges;
        auto& story_targets = services.story_targets;
        auto& mesh_buffers = services.mesh_buffers;
        auto& localization = services.localization;
        auto& native_before = services.native_before;
        auto& compat_error = services.compat_error;
        auto& compat_warning = services.compat_warning;
        auto& encounter_proof = services.encounter_proof;
        auto& movie_fixture = services.movie_fixture;
        auto& install_compat = services.install_compat;
        if (action == Action::FlowReloadHost || action == Action::FlowReturnTitle)
        {
            ReflectedCall state(pawn, L"GetStateID");
            state.run();
            if (!entry.initialized || gameplay.world.Get() != w || castle.armed || map_session.active() ||
                challenge_log.active() || movie_bridge.playing() ||
                w->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                read_function_int_param(state.function, state.parameters.data(), L"ReturnValue") != 1)
            {
                throw std::runtime_error("kh02_reload_host_idle_required");
            }
            if (action == Action::FlowReturnTitle)
            {
                ReflectedCall title(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                    L"BP_GotoTitle");
                title.object(L"WorldContextObject", pawn);
                title.integer(L"InFadeKind", 1);
                title.run();
                return Json{{"return_title_dispatched", true}}.dump();
            }
            // Exercise world replacement without portal/startup preparation.
            // This leaves the story bank intact and never writes a save slot.
            ReflectedCall travel(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"TresMapJump");
            travel.name(L"MapName", L"/Game/Maps/APKH02CastleTown/APKH02CastleTown");
            travel.name(L"TargetTag", L"APKH02CastleStart");
            travel.integer(L"InFadeKind", 1);
            travel.run();
            return Json{{"host_reload_dispatched", true}}.dump();
        }
        if (kh02_diagnostic_is_castle_trigger(action))
        {
            if (!entry.initialized || gameplay.world.Get() != w ||
                w->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
            {
                throw std::runtime_error("kh02_castle_trigger_context");
            }
            auto* trigger = castle.path_trigger.configure(pawn, castle.path_trigger.expanded());
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!is_uobject_valid_fast(trigger) || !is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
            {
                throw std::runtime_error("kh02_castle_trigger_possession");
            }
            ReflectedCall position(trigger, L"K2_GetActorLocation");
            position.run();
            auto* vector = position.param(L"ReturnValue");
            if (!vector || vector->GetElementSize() != native_vector_bytes)
            {
                throw std::runtime_error("kh02_castle_trigger_position");
            }
            const auto* center = castle.path_trigger.expanded() ? castle.path_trigger.road().center.data()
                                                                : castle.path_trigger.original().center.data();
            ReflectedCall direction(trigger, L"GetActorForwardVector");
            direction.run();
            const auto forward = direction.result_vector();
            if (action == Action::FlowCastleTriggerProbe)
            {
                Json samples = Json::array();
                for (const float offset_x : {-2400.f, -800.f, -400.f, 0.f, 400.f, 800.f})
                {
                    for (const float offset_y : {-800.f, -400.f, 0.f, 400.f, 800.f})
                    {
                        ReflectedCall trace(kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary"),
                                            L"LineTraceSingle");
                        trace.object(L"WorldContextObject", pawn);
                        trace.vector(L"Start", center[0] + offset_x, center[1] + offset_y, center[2] + 6000.f);
                        trace.vector(L"End", center[0] + offset_x, center[1] + offset_y, center[2] - 6000.f);
                        trace.boolean(L"bTraceComplex", true);
                        trace.boolean(L"bIgnoreSelf", true);
                        trace.run();
                        Json sample = {{"xy", {center[0] + offset_x, center[1] + offset_y}},
                                       {"hit", trace.boolean_result()}};
                        if (trace.boolean_result())
                        {
                            auto* hit = trace.param(L"OutHit");
                            auto* schema = static_cast<FStructProperty*>(hit)->GetStruct();
                            auto* storage = hit->ContainerPtrToValuePtr<void>(trace.parameters.data());
                            auto* point = find_struct_field_by_name(schema, L"ImpactPoint");
                            auto* normal = find_struct_field_by_name(schema, L"ImpactNormal");
                            if (!point || point->GetElementSize() != native_vector_bytes || !normal ||
                                normal->GetElementSize() != native_vector_bytes)
                            {
                                throw std::runtime_error("kh02_castle_probe_hit_layout");
                            }
                            const auto* position = point->ContainerPtrToValuePtr<float>(storage);
                            const auto* direction = normal->ContainerPtrToValuePtr<float>(storage);
                            sample["point"] = {position[0], position[1], position[2]};
                            sample["normal"] = {direction[0], direction[1], direction[2]};
                        }
                        samples.push_back(std::move(sample));
                    }
                }
                return Json{{"samples", samples}, {"flags", Json::parse(source_flags.snapshot())}}.dump();
            }
            if (action != Action::FlowCastleTriggerInspect)
            {
                ReflectedCall state(pawn, L"GetStateID");
                state.run();
                if (map_session.active() || challenge_log.active() || movie_bridge.playing() ||
                    read_function_int_param(state.function, state.parameters.data(), L"ReturnValue") != 1)
                {
                    throw std::runtime_error("kh02_castle_trigger_idle_required");
                }
                if (action == Action::FlowCastleTriggerStage || action == Action::FlowCastleTriggerTouch ||
                    action == Action::FlowCastleTriggerAirCross)
                {
                    constexpr float approach_distance = 350.f;
                    constexpr float floor_trace_half_height = 6000.f;
                    constexpr float minimum_walkable_normal_z = .7f;
                    constexpr float air_cross_height = 1500.f;
                    constexpr float ground_clearance = 3.f;
                    ReflectedCall move(pawn, L"K2_SetActorLocation");
                    const float approach = action == Action::FlowCastleTriggerTouch ? 0.f : approach_distance;
                    const float x = center[0] - forward[0] * approach;
                    const float y = center[1] - forward[1] * approach;
                    ReflectedCall trace(kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary"),
                                        L"LineTraceSingle");
                    trace.object(L"WorldContextObject", pawn);
                    trace.vector(L"Start", x, y, center[2] + floor_trace_half_height);
                    trace.vector(L"End", x, y, center[2] - floor_trace_half_height);
                    trace.boolean(L"bTraceComplex", true);
                    trace.boolean(L"bIgnoreSelf", true);
                    trace.run();
                    if (!trace.boolean_result())
                    {
                        throw std::runtime_error("kh02_castle_approach_floor_missing");
                    }
                    auto* hit = trace.param(L"OutHit");
                    auto* schema = static_cast<FStructProperty*>(hit)->GetStruct();
                    auto* storage = hit->ContainerPtrToValuePtr<void>(trace.parameters.data());
                    auto* point = find_struct_field_by_name(schema, L"ImpactPoint");
                    auto* normal = find_struct_field_by_name(schema, L"ImpactNormal");
                    if (!point || point->GetElementSize() != native_vector_bytes || !normal ||
                        normal->GetElementSize() != native_vector_bytes ||
                        normal->ContainerPtrToValuePtr<float>(storage)[2] < minimum_walkable_normal_z)
                    {
                        throw std::runtime_error("kh02_castle_approach_floor_unwalkable");
                    }
                    const float height =
                        action == Action::FlowCastleTriggerAirCross ? air_cross_height : ground_clearance;
                    move.vector(L"NewLocation", x, y, point->ContainerPtrToValuePtr<float>(storage)[2] + height);
                    move.boolean(L"bSweep", false);
                    move.boolean(L"bTeleport", true);
                    move.run();
                }
                else if (action == Action::FlowCastleTriggerOriginal || action == Action::FlowCastleTriggerExpanded)
                {
                    castle.path_trigger.configure(pawn, action == Action::FlowCastleTriggerExpanded);
                }
                else if (action != Action::FlowCastleTriggerWalk)
                {
                    throw std::runtime_error("kh02_castle_trigger_action");
                }
                if (action == Action::FlowCastleTriggerWalk || action == Action::FlowCastleTriggerAirCross)
                {
                    constexpr auto air_cross_duration = std::chrono::milliseconds(900);
                    constexpr auto walk_duration = std::chrono::milliseconds(2000);
                    const auto duration =
                        action == Action::FlowCastleTriggerAirCross ? air_cross_duration : walk_duration;
                    timed_inputs.begin_walk(controller, pawn, w, forward[0], forward[1], duration, true);
                }
            }
            ReflectedCall overlap(trigger, L"IsOverlappingActor");
            overlap.object(L"Other", pawn);
            overlap.run();
            ReflectedCall player(pawn, L"SqEX_IsObjTypePlayer");
            player.run();
            ReflectedCall local(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                L"IsLocalTresPlayerPawnBase");
            local.object(L"Actor", pawn);
            local.run();
            ReflectedCall hero_position(pawn, L"K2_GetActorLocation");
            hero_position.run();
            auto* hero_vector = hero_position.param(L"ReturnValue");
            if (!hero_vector || hero_vector->GetElementSize() != native_vector_bytes)
            {
                throw std::runtime_error("kh02_castle_trigger_hero_position");
            }
            const auto* hero_center = hero_vector->ContainerPtrToValuePtr<float>(hero_position.parameters.data());
            return Json{{"hero", path(pawn)},
                        {"trigger", path(trigger)},
                        {"overlapping", overlap.boolean_result()},
                        {"is_player", player.boolean_result()},
                        {"is_local_player", local.boolean_result()},
                        {"enabled", flag(trigger, L"Enable")},
                        {"hero_position", {hero_center[0], hero_center[1], hero_center[2]}},
                        {"trigger_position", position.result_vector()},
                        {"road_reference", path(castle.path_trigger.road_actor())},
                        {"road_center", castle.path_trigger.road().center},
                        {"road_extent", castle.path_trigger.road().extent},
                        {"expanded", castle.path_trigger.expanded()},
                        {"original_extent", castle.path_trigger.original().extent},
                        {"flags", Json::parse(source_flags.snapshot())}}
                .dump();
        }
        if (action == Action::FlowTestHighestPoint)
        {
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            ReflectedCall state(pawn, L"GetStateID");
            state.run();
            if (!entry.initialized || gameplay.world.Get() != w || movie_bridge.playing() ||
                !is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
                w->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                read_function_int_param(state.function, state.parameters.data(), L"ReturnValue") != 1)
            {
                throw std::runtime_error("kh02_highest_point_stage_context");
            }
            UObject* trigger = nullptr;
            for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
            {
                if (actor->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms200.") &&
                    actor->GetName() == L"lv_EventTriggerBox1_WALK")
                {
                    trigger = actor;
                    break;
                }
            }
            if (!is_uobject_valid_fast(trigger))
            {
                throw std::runtime_error("kh02_highest_point_stage_trigger");
            }
            ReflectedCall bounds(trigger, L"GetActorBounds");
            bounds.boolean(L"bOnlyCollidingComponents", true);
            bounds.run();
            auto* origin = bounds.param(L"Origin");
            auto* extent = bounds.param(L"BoxExtent");
            if (!origin || origin->GetElementSize() != native_vector_bytes || !extent ||
                extent->GetElementSize() != native_vector_bytes || timed_inputs.walk_active())
            {
                throw std::runtime_error("kh02_highest_point_stage_bounds");
            }
            const auto* location = origin->ContainerPtrToValuePtr<float>(bounds.parameters.data());
            const auto* half_extent = extent->ContainerPtrToValuePtr<float>(bounds.parameters.data());
            ReflectedCall direction(trigger, L"GetActorForwardVector");
            direction.run();
            const auto forward = direction.result_vector();
            // A teleport inside the overlapping volumes can select CutChange
            // before Walk_Camera is set, bypassing the state-ID wait. Approach
            // from outside all three volumes through normal movement instead.
            constexpr float approach_distance_cm = 350.f;
            constexpr float floor_clearance_cm = 32.f;
            const std::array<float, 3> approach{location[0] - forward[0] * approach_distance_cm,
                                                location[1] - forward[1] * approach_distance_cm,
                                                location[2] - half_extent[2] + floor_clearance_cm};
            ReflectedCall move(pawn, L"K2_SetActorLocation");
            move.vector(L"NewLocation", approach[0], approach[1], approach[2]);
            move.boolean(L"bSweep", false);
            move.boolean(L"bTeleport", true);
            move.run();
            constexpr float approach_input = 0.5f;
            constexpr auto walk_duration = std::chrono::milliseconds(2000);
            timed_inputs.begin_walk(controller, pawn, w, forward[0] * approach_input, forward[1] * approach_input,
                                    walk_duration);
            return Json{{"staged_at_original_trigger", path(trigger)},
                        {"requested_location", approach},
                        {"walking_toward_original_trigger", true},
                        {"position_dispatched", move.boolean_result()}}
                .dump();
        }
        if (action == Action::FlowTestCharacterAqua || action == Action::FlowTestCharacterSora)
        {
            const auto requested_kind = action == Action::FlowTestCharacterAqua ? aqua_player_id : sora_player_id;
            if (!entry.initialized || gameplay.world.Get() != w || movie_bridge.playing() ||
                w->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                !runtime_sora_control_ready(pawn))
            {
                throw std::runtime_error("kh02_castle_test_character_context");
            }
            ReflectedCall change(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"BP_TresGameChangePlayer");
            change.object(L"WorldContextObject", pawn);
            change.integer(L"InKind", requested_kind);
            change.run();
            return Json{{"character_change_dispatched", true}, {"requested_player_kind", requested_kind}}.dump();
        }
        if (action == Action::FlowStarsInspect)
        {
            return castle.shooting_star.snapshot(pawn, within_challenges[kh02_challenge_id::look_shooting_star]).dump();
        }
        if (action == Action::FlowStarsLook || action == Action::FlowStarsLookAway)
        {
            if (!entry.initialized || gameplay.world.Get() != w ||
                w->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
            {
                throw std::runtime_error("kh02_star_camera_context");
            }
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
            {
                throw std::runtime_error("kh02_star_camera_possession");
            }
            UObject* target = nullptr;
            for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
            {
                if (actor->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_challenge.") &&
                    actor->GetName() == L"TargetPoint_ShootingStar")
                {
                    target = actor;
                }
            }
            if (!target)
            {
                throw std::runtime_error("kh02_star_camera_target");
            }
            ReflectedCall origin(pawn, L"K2_GetActorLocation");
            ReflectedCall look(target, L"K2_GetActorLocation");
            origin.run();
            look.run();
            const auto* v = origin.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(origin.parameters.data());
            const auto* t = look.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(look.parameters.data());
            auto* manager = try_read_reflected_object_property_value(controller, L"PlayerCameraManager");
            ReflectedCall normal(manager, L"GetTresCameraNormal");
            normal.run();
            auto* camera = normal.result();
            if (!is_uobject_valid_fast(camera) || kh02_actor_world(camera) != w)
            {
                throw std::runtime_error("kh02_star_camera_missing");
            }
            constexpr float eye_height = 80.f;
            constexpr float half_turn_degrees = 180.f;
            const float dx = t[0] - v[0];
            const float dy = t[1] - v[1];
            const float dz = t[2] - v[2] - eye_height;
            const float rotation[]{std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * half_turn_degrees /
                                       std::numbers::pi_v<float>,
                                   std::atan2(dy, dx) * half_turn_degrees / std::numbers::pi_v<float> +
                                       (action == Action::FlowStarsLookAway ? half_turn_degrees : 0.f),
                                   0};
            ReflectedCall aim(camera, L"SetAimRotation");
            auto* prop = aim.param(L"Rot");
            if (!prop || prop->GetElementSize() != native_vector_bytes)
            {
                throw std::runtime_error("kh02_star_camera_rotation");
            }
            std::memcpy(prop->ContainerPtrToValuePtr<void>(aim.parameters.data()), rotation, 12);
            aim.boolean(L"bWorld", true);
            aim.number(L"Time", 0);
            aim.run();
            return castle.shooting_star.snapshot(pawn, within_challenges[kh02_challenge_id::look_shooting_star]).dump();
        }
        if (action == Action::FlowChestProbe || action == Action::FlowChestBind || action == Action::FlowChestPress ||
            action == Action::FlowChestApproach || action == Action::FlowChestWalk)
        {
            auto* cls =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresTreasureBox");
            ReflectedCall origin(pawn, L"K2_GetActorLocation");
            origin.run();
            auto* v = origin.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(origin.parameters.data());
            Json proof = {{"chests", Json::array()}};
            UObject* selected = nullptr;
            float best = action == Action::FlowChestApproach ? std::numeric_limits<float>::max() : 350.f * 350.f;
            for (auto* chest : find_live_actors_of_class(pawn, cls, 128))
            {
                if (kh02_actor_world(chest) != w || !chest->GetPathName().starts_with(L"/Game/Zevels/dw/"))
                {
                    continue;
                }
                auto* reactor = try_read_reflected_object_property_value(chest, L"MyReactor");
                ReflectedCall pos(chest, L"K2_GetActorLocation");
                pos.run();
                auto* p = pos.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(pos.parameters.data());
                const float d =
                    (p[0] - v[0]) * (p[0] - v[0]) + (p[1] - v[1]) * (p[1] - v[1]) + (p[2] - v[2]) * (p[2] - v[2]);
                ReflectedCall overlap(reactor, L"IsOverlappingActor");
                overlap.object(L"Other", pawn);
                overlap.run();
                proof["chests"].push_back({{"actor", path(chest)},
                                           {"distance", std::sqrt(d)},
                                           {"recorded", reconcile_chest_record(chest, false)},
                                           {"active", flag(chest, L"m_bIsActive")},
                                           {"reactor_active", flag(reactor, L"m_bIsGimmickActive")},
                                           {"overlapping", overlap.boolean_result()}});
                if (d < best && !flag(chest, L"bHidden") && flag(chest, L"m_bIsActive") &&
                    flag(reactor, L"m_bIsGimmickActive") && !reconcile_chest_record(chest, false))
                {
                    best = d;
                    selected = chest;
                }
            }
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            if (action == Action::FlowChestApproach || action == Action::FlowChestWalk)
            {
                if (!is_uobject_valid_fast(selected) || !is_uobject_valid_fast(controller) ||
                    try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
                    timed_inputs.walk_active() || timed_inputs.action_active())
                {
                    throw std::runtime_error("kh02_chest_fixture_ready_player_required");
                }
                ReflectedCall state(pawn, L"GetStateID");
                state.run();
                const auto state_id =
                    read_function_int_param(state.function, state.parameters.data(), L"ReturnValue").value_or(-1);
                if (state_id != kh02_chest_interaction::idle_state)
                {
                    throw std::runtime_error("kh02_chest_fixture_idle_player_required");
                }
                ReflectedCall direction(pawn, L"GetActorForwardVector");
                direction.run();
                const auto forward = direction.result_vector();
                if (action == Action::FlowChestApproach)
                {
                    ReflectedCall chest_position(selected, L"K2_GetActorLocation");
                    chest_position.run();
                    const auto position = chest_position.result_vector();
                    constexpr float approach_distance_cm = 110.f;
                    // TresRoot is not an ACharacter capsule. Let native movement
                    // settle the hero onto the authored chest floor.
                    constexpr float approach_height_clearance_cm = 200.f;
                    ReflectedCall move(pawn, L"K2_SetActorLocation");
                    move.vector(L"NewLocation", position[0] - forward[0] * approach_distance_cm,
                                position[1] - forward[1] * approach_distance_cm,
                                position[2] + approach_height_clearance_cm);
                    move.boolean(L"bSweep", false);
                    move.boolean(L"bTeleport", true);
                    move.run();
                    proof["approached"] = path(selected);
                    proof["position_dispatched"] = move.boolean_result();
                }
                else
                {
                    constexpr float approach_input = 0.5f;
                    constexpr auto walk_duration = std::chrono::milliseconds(1800);
                    timed_inputs.begin_walk(controller, pawn, w, forward[0] * approach_input,
                                            forward[1] * approach_input, walk_duration);
                    proof["walk_toward"] = path(selected);
                }
            }
            if (action == Action::FlowChestBind || action == Action::FlowChestPress)
            {
                if (!is_uobject_valid_fast(selected))
                {
                    throw std::runtime_error("kh02_near_unopened_chest_required");
                }
                if (action == Action::FlowChestBind)
                {
                    ReflectedCall bind(controller, L"BP_NotifySetActionCommand1");
                    bind.integer(L"inCommand", player_action_command_value(PlayerActionCommand::OPEN));
                    bind.number(L"inTimer", 2);
                    bind.object(L"inActor", selected);
                    bind.object(L"InComponent", try_read_reflected_object_property_value(selected, L"MyReactor"));
                    bind.run();
                    proof["bound"] = path(selected);
                }
                else
                {
                    ReflectedCall state(pawn, L"GetStateID");
                    state.run();
                    if (read_function_int_param(state.function, state.parameters.data(), L"ReturnValue").value_or(-1) !=
                            1 ||
                        timed_inputs.action_active())
                    {
                        throw std::runtime_error("kh02_chest_press_requires_idle_hero");
                    }
                    timed_inputs.press(controller, action == Action::FlowMenu ? L"menu" : L"interact");
                }
            }
            ReflectedCall state(pawn, L"GetStateID");
            state.run();
            proof["hero_state"] =
                read_function_int_param(state.function, state.parameters.data(), L"ReturnValue").value_or(-1);
            proof["hero"] = path(pawn);
            proof["controller"] = path(controller);
            proof["command_info"] = path(try_read_reflected_object_property_value(controller, L"m_pUICommandInfo"));
            return proof.dump();
        }
        if (action == Action::FlowTargetVisibilityTest)
        {
            return story_targets.visibility_fixture(w, [&] { map_session.observe_world(w); }).dump();
        }
        if (action == Action::FlowPrepare || kh02_diagnostic_is_flow_load(action))
        {
            // Original gear particles sample skeletal surfaces on the CPU.
            // Keep their index/skin buffers before loading the source meshes.
            // Setting this after InitRHI cannot recover an already freed copy.
            mesh_buffers.retain(pawn);
            localization.prepare();
            gameplay.pawn = make_uobject_weak(pawn);
            if (gameplay.world.Get() != w)
            {
                native_before = kh02_native_flags();
                source_flags.activate(w);
                install_compat();
                castle.streams.clear();
                gameplay.started = false;
                gameplay.script = {};
                castle.gimmick_script = {};
                gameplay.world = make_uobject_weak(w);
            }
            if (castle.streams.empty())
            {
                auto* p = w->GetPropertyByNameInChain(L"StreamingLevels");

                struct Array
                {
                    UObject** data;
                    int32_t count, capacity;
                };

                if (!p || !p->IsA<FArrayProperty>() || p->GetElementSize() != sizeof(Array))
                {
                    throw std::runtime_error("kh02_flow_stream_array");
                }
                auto* array = p->ContainerPtrToValuePtr<Array>(w);
                if (array->count < 22 || array->count > 64 || array->capacity < array->count || !array->data)
                {
                    throw std::runtime_error("kh02_flow_host_missing_streams");
                }
                for (int i = 18; i < 22; ++i)
                {
                    auto* s = array->data[i];
                    if (!is_uobject_valid_fast(s) || s->GetOuterPrivate() != w)
                    {
                        throw std::runtime_error("kh02_flow_stream_owner");
                    }
                    castle.streams.push_back(make_uobject_weak(s));
                }
            }
            if (action == Action::FlowPrepare)
            {
                return services.snapshot().dump();
            }
            const int only = action == Action::FlowLoadGameflow  ? 0
                             : action == Action::FlowLoadGimmick ? 1
                             : action == Action::FlowLoadCamera  ? 2
                             : action == Action::FlowLoadEnemy   ? 3
                                                                 : -1;
            for (size_t i = 0; i < castle.streams.size(); ++i)
            {
                if (only >= 0 && i != static_cast<size_t>(only))
                {
                    continue;
                }
                auto* s = castle.streams[i].Get();
                set_flag(s, L"bShouldBeLoaded", true);
                set_flag(s, L"bShouldBeVisible", true);
                ReflectedCall visible(s, L"IsLevelVisible");
                visible.run();
                if (!visible.boolean_result())
                {
                    return std::nullopt;
                }
                auto* level = try_read_reflected_object_property_value(s, L"LoadedLevel");
                auto* ls = try_read_reflected_object_property_value(level, L"LevelScriptActor");
                if (i == 0)
                {
                    if (!is_uobject_valid_fast(ls))
                    {
                        throw std::runtime_error("kh02_flow_script_missing");
                    }
                    gameplay.script = make_uobject_weak(ls);
                    map_session.bind_source_script(ls);
                }
                if (i == 1)
                {
                    if (!is_uobject_valid_fast(ls))
                    {
                        throw std::runtime_error("kh02_gimmick_script_missing");
                    }
                    castle.gimmick_script = make_uobject_weak(ls);
                }
            }
        }
        else if (action == Action::FlowResumeGears)
        {
            if (!entry.initialized || gameplay.world.Get() != w || gameplay.pawn.Get() != pawn ||
                native_before != kh02_native_flags() ||
                source_flags.value(L"gameflow_DW") != kh02_checkpoint::gear_scene_dw011)
            {
                throw std::runtime_error("kh02_resume_requires_fresh_entry");
            }
            const auto saved =
                *kh02_read_runtime_config(L"kh02_story_checkpoint.json", kh02_checkpoint_config_limit_bytes, false,
                                          "kh02_resume_image", "kh02_resume_size");
            if (saved.at("schema") != "kh02_collected_gears.v1" ||
                saved.at("world") != "/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                saved.at("gear_count") != 4)
            {
                throw std::runtime_error("kh02_resume_evidence");
            }
            auto* script = gameplay.script.Get();
            auto* count = script->GetPropertyByNameInChain(L"dw_02_gear_count");
            if (!count || count->GetClass().GetName() != L"IntProperty" || count->GetElementSize() != 4)
            {
                throw std::runtime_error("kh02_resume_count_layout");
            }
            source_flags.restore_collected_gears(saved.at("flags").dump());
            *count->ContainerPtrToValuePtr<int32_t>(script) = 4;
            UObject* checkpoint = nullptr;
            std::vector<UObject*> starts;
            UObjectGlobals::FindAllOf(L"TresPlayerStart", starts);
            for (auto* candidate : starts)
            {
                if (is_uobject_valid_fast(candidate) && candidate->GetOuterPrivate() == script->GetOuterPrivate() &&
                    candidate->GetName() == L"TresPlayerStart_AutoSave_06")
                {
                    if (checkpoint)
                    {
                        throw std::runtime_error("kh02_resume_checkpoint_ambiguous");
                    }
                    checkpoint = candidate;
                }
            }
            {
                auto* property = script->GetPropertyByNameInChain(L"CheckPointActor");
                if (!is_uobject_valid_fast(checkpoint) || kh02_actor_world(checkpoint) != w || !property ||
                    property->GetClass().GetName() != L"ObjectProperty" || property->GetElementSize() != 8)
                {
                    throw std::runtime_error("kh02_resume_checkpoint");
                }
                *property->ContainerPtrToValuePtr<UObject*>(script) = checkpoint;
            }
            ReflectedCall check_gears(castle.gimmick_script.Get(), L"dw_02_gameflow_REvt_CheckGear");
            check_gears.run();
            auto* mapset = find_loaded_runtime_asset(L"/Game/Zevels/dw/dw_02/mapset/ms_dw_02.ms_dw_02");
            int uuid = resume_mapset_request_base;
            for (const auto* command : {L"dw02_Get_Style", L"dw02_Get_Slide", L"dw02_Get_Shoot", L"dw02_Get_Flight"})
            {
                kh02_apply_mapset(script, mapset, command, uuid);
                ++uuid;
            }
            compat_error.clear();
            compat_warning.clear();
            ReflectedCall resume(script, L"ExecuteUbergraph_dw_02_gameflow");
            resume.integer(L"EntryPoint", 3079);
            resume.run();
            encounter_proof["resumed_captured_gears"] = saved;
        }

        else if (action == Action::FlowStart)
        {
            if (movie_fixture)
            {
                throw std::runtime_error("kh02_movie_fixture_must_be_stopped");
            }
            if (gameplay.started || !is_uobject_valid_fast(castle.gimmick_script.Get()) ||
                !is_uobject_valid_fast(gameplay.script.Get()))
            {
                throw std::runtime_error("kh02_flow_start_state");
            }
            // This diagnostic starts after the source's opening collapse at
            // zero gears. The scenery-only host initially shows every ground
            // layer, so initialize the four repairable layers via the original
            // source graph's exact visibility-only events. These change no
            // story flags and emit no movie/Matinee completion callbacks.
            if (gameplay.world.Get() != w || gameplay.pawn.Get() != pawn)
            {
                throw std::runtime_error("kh02_ground_checkpoint_context");
            }
            for (const auto* suffix : {L"B", L"C", L"D", L"E"})
            {
                const auto event = L"cutscene_dw_02_dw012_REvt_hideGround_" + std::wstring(suffix);
                ReflectedCall hide(gameplay.script.Get(), event.c_str());
                if (hide.function->GetParmsSize() != 0)
                {
                    throw std::runtime_error("kh02_ground_visibility_event_signature");
                }
                hide.run();
            }
            const auto grounds = kh02_ground_streams(w);
            for (const auto* suffix : {"B", "C", "D", "E"})
            {
                const auto expected = std::string("/Game/Zaps/dw/umap/dw_02/dw_02_Ground_") + suffix;
                bool matched = false;
                for (const auto& level : grounds)
                {
                    if (level["package"] == expected)
                    {
                        matched = true;
                        if (level["requested_visible"].get<bool>())
                        {
                            throw std::runtime_error("kh02_ground_visibility_request_not_applied");
                        }
                    }
                }
                if (!matched)
                {
                    throw std::runtime_error("kh02_ground_checkpoint_stream_missing");
                }
            }
            ReflectedCall check_gears(castle.gimmick_script.Get(), L"dw_02_gameflow_REvt_CheckGear");
            check_gears.run();
            ReflectedCall show(castle.gimmick_script.Get(), L"dw_02_gameflow_REvt_showGear");
            show.run();
            gameplay.started = true;
        }
        else if (action == Action::FlowCapture)
        {
            ReflectedCall capture(kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary"),
                                  L"ExecuteConsoleCommand");
            capture.object(L"WorldContextObject", pawn);
            capture.string(L"Command", L"Shot showui");
            capture.run();
        }
        else if (action == Action::FlowInventory || action == Action::FlowInventoryHealthFixture)
        {
            if (!entry.initialized || Kh02Story::current_hero() != pawn || kh02_actor_world(pawn) != w ||
                (pawn->GetClassPrivate()->GetName() != L"p_ex001_Pawn_C" &&
                 pawn->GetClassPrivate()->GetName() != L"p_ex002_Pawn_C"))
            {
                throw std::runtime_error("kh02_inventory_requires_current_hero");
            }
            const auto read_stat = [&](const wchar_t* method)
            {
                ReflectedCall query(pawn, method);
                query.run();
                const auto value = read_function_int_param(query.function, query.parameters.data(), L"ReturnValue");
                if (!value || *value < 0 || *value > std::numeric_limits<int32_t>::max())
                {
                    throw std::runtime_error("kh02_inventory_stat_read_failed");
                }
                return static_cast<int32_t>(*value);
            };
            Json counts = Json::object();
            counts["hp_before"] = read_stat(L"GetHitPoint");
            counts["max_hp"] = read_stat(L"GetMaxHitPoint");
            if (action == Action::FlowInventoryHealthFixture)
            {
                const bool playable_aqua = Kh02Story::persistent_reward_context() == pawn;
                const bool idle_sora =
                    pawn->GetClassPrivate()->GetName() == L"p_ex001_Pawn_C" && read_stat(L"GetStateID") == 1;
                if ((!playable_aqua && !idle_sora) || counts["max_hp"].get<int>() < 4)
                {
                    throw std::runtime_error("kh02_inventory_health_requires_playable_aqua_or_idle_sora");
                }
                ReflectedCall set_health(pawn, L"SetHitPoint");
                if (set_health.function->GetOuterPrivate()->GetName() != L"TresCharPawnBase")
                {
                    throw std::runtime_error("kh02_inventory_health_owner_mismatch");
                }
                const int target = counts["max_hp"].get<int>() / 2;
                set_health.integer(L"InHP", target);
                set_health.run();
                if (read_stat(L"GetHitPoint") != target)
                {
                    throw std::runtime_error("kh02_inventory_health_readback_mismatch");
                }
            }
            counts["hp"] = read_stat(L"GetHitPoint");
            for (const auto* item : {L"ITEM_POTION", L"ITEM_ETHER"})
            {
                ReflectedCall query(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                    L"BP_TresGetBackyardItemNum");
                query.object(L"WorldContextObject", pawn);
                query.name(L"InItemKeyName", item);
                query.run();
                auto value = read_function_int_param(query.function, query.parameters.data(), L"ReturnValue");
                if (!value || *value < 0)
                {
                    throw std::runtime_error("kh02_inventory_read_failed");
                }
                counts[to_utf8(item)] = *value;
            }
            return counts.dump();
        }
        else if (action == Action::FlowButton || action == Action::FlowMenu)
        {
            if (!gameplay.started || timed_inputs.action_active())
            {
                throw std::runtime_error("kh02_flow_button_state");
            }
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller))
            {
                throw std::runtime_error("kh02_flow_controller");
            }
            timed_inputs.press(controller, action == Action::FlowMenu ? L"menu" : L"interact");
        }
        else if (action != Action::FlowInspect)
        {
            throw std::runtime_error("kh02_flow_action");
        }
        return services.snapshot().dump();
    }
} // namespace kh3ap
