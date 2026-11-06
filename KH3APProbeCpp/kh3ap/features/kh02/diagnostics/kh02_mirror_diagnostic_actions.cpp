#include "kh02_diagnostic_action_helpers.h"
#include "kh3ap/features/kh02/diagnostics/kh02_capture_path.h"
#include "kh3ap/features/kh02/story/kh02_mirror_gameplay_session.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh3ap/features/kh02/story/kh02_timed_inputs.h"
#include "kh3ap/features/kh02/world/kh02_mapset.h"
#include "kh3ap/core/input/runtime_player_control.h"

namespace kh3ap
{
    using namespace kh02diagnostic_detail;

    namespace
    {
        constexpr float mirror_face_radius_cm = 350.0f;
        constexpr float minimum_facing_distance_squared = 1.0f;
        constexpr float half_turn_degrees = 180.0f;
        constexpr float pi_radians = 3.14159265358979323846f;
        constexpr float full_turn_degrees = 360.0f;
        constexpr float facing_tolerance_degrees = 1.0f;
        constexpr float core_approach_x_offset_cm = 100.0f;
        constexpr float core_approach_z_offset_cm = 50.0f;
        constexpr float exit_forward_vertical_tolerance = 0.05f;
        constexpr float exit_approach_minimum_cm = 60.0f;
        constexpr float exit_approach_maximum_cm = 1200.0f;
        constexpr float exit_edge_margin_cm = 100.0f;
    } // namespace

    auto Kh02MirrorDiagnosticActions::execute(UObject* pawn, UObject* w, Action action,
                                              Kh02MirrorActionServices services) -> std::optional<std::string>
    {
        auto& mirror = services.mirror;
        auto& source_flags = services.source_flags;
        auto& timed_inputs = services.timed_inputs;
        auto& postboss_matinee = services.postboss_matinee;

        if (!is_uobject_valid_fast(w) || w != mirror.world.Get())
        {
            throw std::runtime_error("kh02_within_context");
        }
        if (action == Action::WithinCapture)
        {
            const auto file = kh02_capture_file(L"kh02-world-within-scene.png");
            ReflectedCall capture(kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary"),
                                  L"ExecuteConsoleCommand");
            capture.object(L"WorldContextObject", pawn);
            capture.string(L"Command", kh02_capture_command(file));
            capture.run();
            auto snapshot = services.snapshot();
            snapshot["screenshot_file"] = to_utf8(file.wstring());
            return snapshot.dump();
        }
        else if (action == Action::WithinCaptureUi)
        {
            ReflectedCall capture(kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary"),
                                  L"ExecuteConsoleCommand");
            capture.object(L"WorldContextObject", pawn);
            capture.string(L"Command", L"Shot showui");
            capture.run();
        }
        else if (action == Action::WithinReplay || action == Action::WithinReplayDw020)
        {
            const bool mirror_scene = action == Action::WithinReplayDw020;
            if (w->GetPathName() != L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin" ||
                !mirror.initialized ||
                (is_uobject_valid_fast(postboss_matinee.Get()) && flag(postboss_matinee.Get(), L"bIsPlaying")))
            {
                throw std::runtime_error("kh02_within_replay_context");
            }
            ReflectedCall get(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"),
                              L"GetStreamingLevel");
            get.object(L"WorldContextObject", pawn);
            get.name(L"PackageName", mirror_scene ? L"cutscene_dw03_dw020" : L"cutscene_dw03_dw016");
            get.run();
            auto* stream = get.result();
            if (!is_uobject_valid_fast(stream))
            {
                throw std::runtime_error("kh02_within_replay_stream");
            }
            set_flag(stream, L"bShouldBeLoaded", true);
            set_flag(stream, L"bShouldBeVisible", true);
            ReflectedCall visible(stream, L"IsLevelVisible");
            visible.run();
            if (!visible.boolean_result())
            {
                return std::nullopt;
            }
            auto* level = try_read_reflected_object_property_value(stream, L"LoadedLevel");
            auto* script = try_read_reflected_object_property_value(level, L"LevelScriptActor");
            if (!is_uobject_valid_fast(script))
            {
                return std::nullopt;
            }
            UObject* scene = nullptr;
            for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
            {
                if (actor->GetOuterPrivate() == level && actor->GetName() == (mirror_scene ? L"dw020" : L"dw016") &&
                    actor->GetClassPrivate()->GetName() == L"MatineeActor")
                {
                    if (scene)
                    {
                        throw std::runtime_error("kh02_within_replay_matinee_ambiguous");
                    }
                    scene = actor;
                }
            }
            if (!is_uobject_valid_fast(scene))
            {
                throw std::runtime_error("kh02_within_replay_matinee");
            }
            ReflectedCall rewind(scene, L"SetPosition");
            rewind.number(L"NewPosition", 0);
            rewind.boolean(L"bJump", true);
            rewind.run();
            ReflectedCall cinematic(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                    L"TresStartCinematicMode");
            cinematic.object(L"WorldContextObject", pawn);
            cinematic.run();
            // Visual-only replay for lighting inspection. Story travel
            // tests must use normal source progression.
            ReflectedCall replay(scene, L"Play");
            replay.run();
        }
        else if (action == Action::WithinFace)
        {
            UObject* nearest = nullptr;
            float nearest_distance_squared = mirror_face_radius_cm * mirror_face_radius_cm;
            ReflectedCall origin(pawn, L"K2_GetActorLocation");
            origin.run();
            auto* vp = origin.param(L"ReturnValue");
            if (!vp || vp->GetElementSize() != native_vector_bytes)
            {
                throw std::runtime_error("kh02_mirror_face_position_layout");
            }
            auto* v = vp->ContainerPtrToValuePtr<float>(origin.parameters.data());
            float yaw{};
            for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
            {
                if (!actor->GetPathName().starts_with(L"/Game/Zevels/dw/") || flag(actor, L"bHidden"))
                {
                    continue;
                }
                const auto name = actor->GetName();
                if (!name.starts_with(L"g_dw_Mirror") && !name.starts_with(L"g_dw_ColumnMirror"))
                {
                    continue;
                }
                auto* reactor = try_read_reflected_object_property_value(actor, L"TresReactor");
                // Pillar mirrors must first be broken with ordinary attacks.
                // Facing them does not require their Check reaction to be active.
                if (!is_uobject_valid_fast(reactor) ||
                    (!flag(reactor, L"m_bIsGimmickActive") && !name.starts_with(L"g_dw_ColumnMirror")))
                {
                    continue;
                }
                ReflectedCall target(actor, L"K2_GetActorLocation");
                target.run();
                auto* p = target.param(L"ReturnValue");
                if (!p || p->GetElementSize() != native_vector_bytes)
                {
                    continue;
                }
                auto* t = p->ContainerPtrToValuePtr<float>(target.parameters.data());
                const float delta_x = t[0] - v[0];
                const float delta_y = t[1] - v[1];
                const float delta_z = t[2] - v[2];
                const float horizontal_distance_squared = delta_x * delta_x + delta_y * delta_y;
                const float distance_squared = horizontal_distance_squared + delta_z * delta_z;
                if (distance_squared < nearest_distance_squared &&
                    horizontal_distance_squared > minimum_facing_distance_squared)
                {
                    nearest_distance_squared = distance_squared;
                    nearest = actor;
                    yaw = std::atan2(delta_y, delta_x) * half_turn_degrees / pi_radians;
                }
            }
            if (!nearest)
            {
                throw std::runtime_error("kh02_mirror_face_requires_nearby_active_mirror");
            }
            ReflectedCall face(pawn, L"K2_SetActorRotation");
            auto* p = face.param(L"NewRotation");
            if (!p || p->GetElementSize() != native_vector_bytes)
            {
                throw std::runtime_error("kh02_mirror_face_rotation_layout");
            }
            const float rotation[]{0, yaw, 0};
            std::memcpy(p->ContainerPtrToValuePtr<void>(face.parameters.data()), rotation, sizeof(rotation));
            face.boolean(L"bTeleportPhysics", true);
            face.run();
            if (!face.boolean_result())
            {
                throw std::runtime_error("kh02_mirror_face_rejected");
            }
            ReflectedCall read(pawn, L"K2_GetActorRotation");
            read.run();
            p = read.param(L"ReturnValue");
            if (!p || p->GetElementSize() != native_vector_bytes ||
                std::abs(std::remainder(p->ContainerPtrToValuePtr<float>(read.parameters.data())[1] - yaw,
                                        full_turn_degrees)) > facing_tolerance_degrees)
            {
                throw std::runtime_error("kh02_mirror_face_readback");
            }
        }
        else if (action == Action::WithinTestApproachCore)
        {
            if (w->GetPathName() != L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands")
            {
                throw std::runtime_error("kh02_test_core_context");
            }
            UObject* boss = nullptr;
            for (auto* a : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
            {
                if (a->GetPathName().starts_with(L"/Game/Zevels/dw/dw_10/umap/dw_10_enemy.") &&
                    a->GetName().starts_with(L"e_dw401_Pawn_C"))
                {
                    boss = a;
                }
            }
            if (!boss || !flag(boss, L"m_bCoreDisp"))
            {
                throw std::runtime_error("kh02_test_core_not_exposed");
            }
            auto* mesh = try_read_reflected_object_property_value(boss, L"MyMesh");
            if (!is_uobject_valid_fast(mesh))
            {
                throw std::runtime_error("kh02_test_core_mesh");
            }
            ReflectedCall position(mesh, L"GetSocketLocation");
            position.name(L"InSocketName", L"center");
            position.run();
            auto* result = position.param(L"ReturnValue");
            if (!result || result->GetElementSize() != native_vector_bytes)
            {
                throw std::runtime_error("kh02_test_core_position");
            }
            const auto* v = result->ContainerPtrToValuePtr<float>(position.parameters.data());
            if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
            {
                throw std::runtime_error("kh02_test_core_finite");
            }
            ReflectedCall move(pawn, L"K2_SetActorLocation");
            move.vector(L"NewLocation", v[0] - core_approach_x_offset_cm, v[1], v[2] - core_approach_z_offset_cm);
            move.boolean(L"bSweep", false);
            move.boolean(L"bTeleport", true);
            move.run();
        }
        else if (action == Action::WithinTestCharacterAqua)
        {
            if (w->GetPathName() != L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands" ||
                !runtime_sora_control_ready(pawn))
            {
                throw std::runtime_error("kh02_test_character_context");
            }
            ReflectedCall change(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"BP_TresGameChangePlayer");
            change.object(L"WorldContextObject", pawn);
            change.integer(L"InKind", aqua_player_kind);
            change.run();
        }
        else if (action == Action::WithinTestLeaveIslandsBattle)
        {
            if (w->GetPathName() != L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands" ||
                source_flags.value(L"gameflow_DW") != kh02_checkpoint::demon_tide_start ||
                source_flags.value(L"gameflow_DW_VISITED_9") != 1 || !runtime_sora_control_ready(pawn))
            {
                throw std::runtime_error("kh02_test_leave_battle_context");
            }
            ReflectedCall mode(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                               L"TresSetSpecialBattleMode");
            mode.object(L"WorldContextObject", pawn);
            mode.boolean(L"bIsSpecialMode", false);
            mode.run();
            ReflectedCall travel(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"TresMapJump");
            travel.name(L"MapName", L"/Game/Maps/APKH02Mirrors/APKH02Depths");
            travel.name(L"TargetTag", L"dw_09_Lv_AutoSave_02");
            travel.integer(L"InFadeKind", 1);
            travel.run();
        }
        else if (action == Action::WithinPrepareForestExit)
        {
            if (w->GetPathName() != L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest" ||
                source_flags.value(L"gameflow_DW") != 350 || !runtime_sora_control_ready(pawn))
            {
                throw std::runtime_error("kh02_forest_exit_fixture_context");
            }
            // Fixture setup only: load the source's completed final area before
            // staging beside its exit. Travel still requires the real overlap.
            auto* mapset = kh02_require_loaded_object(L"/Game/Zevels/dw/dw_08/mapset/ms_dw_08.ms_dw_08");
            kh02_apply_mapset(pawn, mapset, L"dw_StartArea_I", 0x4b483700);
            kh02_apply_mapset(pawn, mapset, L"dw_ClearArea_I", 0x4b483701);
        }
        else if (action == Action::WithinWalkDepthsExit || action == Action::WithinWalkForestExit)
        {
            const bool forest = action == Action::WithinWalkForestExit;
            const auto* expected_world = forest ? L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest"
                                                : L"/Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths";
            const auto* expected_door =
                forest ? L"/Game/Zevels/dw/dw_08/umap/"
                         L"dw_08_gameflow.dw_08_gameflow:PersistentLevel.TresMapJump_dw025a_Before"
                       : L"/Game/Zevels/dw/dw_09/umap/"
                         L"dw_09_gameflow.dw_09_gameflow:PersistentLevel.TresMapJumpDirectionalTriggerBox_1";
            if (w->GetPathName() != expected_world || timed_inputs.walk_active())
            {
                throw std::runtime_error("kh02_exit_walk_context");
            }
            UObject* door = nullptr;
            for (auto* a : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
            {
                if (a->GetPathName() == expected_door)
                {
                    door = a;
                }
            }
            if (!door || !flag(door, L"bActorEnableCollision"))
            {
                throw std::runtime_error("kh02_exit_walk_door");
            }
            auto* component = try_read_reflected_object_property_value(door, L"CollisionComponent");
            if (!is_uobject_valid_fast(component) || !flag(component, L"bGenerateOverlapEvents"))
            {
                throw std::runtime_error("kh02_exit_walk_collision");
            }
            auto vector = [](UObject* o, const wchar_t* function)
            {
                ReflectedCall c(o, function);
                c.run();
                auto* p = c.param(L"ReturnValue");
                if (!p || p->GetElementSize() != native_vector_bytes)
                {
                    throw std::runtime_error("kh02_exit_walk_vector");
                }
                auto* v = p->ContainerPtrToValuePtr<float>(c.parameters.data());
                return std::array<float, 3>{v[0], v[1], v[2]};
            };
            const auto origin = vector(door, L"K2_GetActorLocation");
            const auto forward = vector(door, forest ? L"GetActorRightVector" : L"GetActorForwardVector");
            const auto extent = vector(component, L"GetScaledBoxExtent");
            const float along_extent = forest ? extent[1] : extent[0];
            const float lateral_extent = forest ? extent[0] : extent[1];
            const auto position = vector(pawn, L"K2_GetActorLocation");
            const float delta_x = position[0] - origin[0];
            const float delta_y = position[1] - origin[1];
            const float along = delta_x * forward[0] + delta_y * forward[1];
            const float lateral = -delta_x * forward[1] + delta_y * forward[0];
            if (std::abs(forward[2]) > exit_forward_vertical_tolerance ||
                along < along_extent + exit_approach_minimum_cm || along > along_extent + exit_approach_maximum_cm ||
                std::abs(lateral) > lateral_extent - exit_edge_margin_cm ||
                std::abs(position[2] - origin[2]) > extent[2] - exit_edge_margin_cm)
            {
                throw std::runtime_error("kh02_exit_walk_inside_approach_required");
            }
            auto* c = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!is_uobject_valid_fast(c) || try_read_reflected_object_property_value(c, L"Pawn") != pawn)
            {
                throw std::runtime_error("kh02_exit_walk_controller");
            }
            timed_inputs.begin_walk(c, pawn, w, -forward[0], -forward[1]);
        }
        else if (action == Action::WithinInteract || action == Action::WithinCancel || action == Action::WithinMenu ||
                 action == Action::WithinTestAttack || action == Action::WithinTestJump)
        {
            auto* c = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!is_uobject_valid_fast(c))
            {
                throw std::runtime_error("kh02_within_interact");
            }
            if (timed_inputs.action_active())
            {
                throw std::runtime_error("kh02_within_input_pending");
            }
            const auto* selected_action = action == Action::WithinCancel       ? L"command_cancel"
                                          : action == Action::WithinMenu       ? L"menu"
                                          : action == Action::WithinTestAttack ? L"attack"
                                          : action == Action::WithinTestJump   ? L"jump"
                                                                               : L"interact";
            timed_inputs.press(c, selected_action);
        }
        else if (action != Action::WithinInspect)
        {
            throw std::runtime_error("kh02_within_action");
        }
        return services.snapshot().dump();
    }

} // namespace kh3ap
