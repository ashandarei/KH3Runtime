#include "kh02_story_diagnostics.h"
#include "kh02_runtime_inspection.h"
#include "kh3ap/features/kh02/story/kh02_castle_gameplay_session.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_tracker.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_observer.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_session.h"
#include "kh3ap/features/kh02/wardrobe/kh02_dressing_room.h"
#include "kh3ap/features/kh02/combat/kh02_enemy_stats.h"
#include "kh3ap/features/kh02/story/kh02_entry_session.h"
#include "kh3ap/features/kh02/cutscenes/kh02_event_aqua.h"
#include "kh3ap/features/kh02/story/kh02_gameplay_binding.h"
#include "kh3ap/features/kh02/ui/kh02_hud.h"
#include "kh3ap/features/kh02/ui/kh02_information_queue.h"
#include "kh3ap/features/kh02/cutscenes/kh02_intro_presentation.h"
#include "kh3ap/features/kh02/world/kh02_island_materials.h"
#include "kh3ap/features/kh02/core/kh02_localization.h"
#include "kh3ap/features/kh02/story/kh02_map_session.h"
#include "kh3ap/features/kh02/core/kh02_mesh_buffer_lease.h"
#include "kh3ap/features/kh02/story/kh02_mirror_gameplay_session.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include "kh3ap/features/kh02/movies/kh02_movie_probe.h"
#include "kh3ap/features/kh02/cutscenes/kh02_opening_followup.h"
#include "kh3ap/features/kh02/cutscenes/kh02_opening_scene.h"
#include "kh3ap/features/kh02/progress/kh02_progress_session.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh3ap/features/kh02/story/kh02_story_music.h"
#include "kh3ap/features/kh02/story/kh02_story_targets.h"
#include "kh3ap/features/kh02/cutscenes/kh02_matinee_binding.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_reflection.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;
    using kh02cutscene_detail::flag;
    using kh02cutscene_detail::real;

    namespace
    {
        constexpr std::size_t native_vector_bytes = 3 * sizeof(float);
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr int actor_scan_limit = 16'384;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }
    } // namespace

    auto Kh02StoryDiagnostics::flow_snapshot(UObject* pawn, const Kh02FlowDiagnosticsView& view) -> Json
    {
        auto& source_flags = view.common.source_flags;
        auto& hud = view.common.hud;
        auto& enemy_stats = view.common.enemy_stats;
        auto& challenge_tracker = view.common.challenge_tracker;
        auto& chest_observer = view.common.chest_observer;
        auto& progress_session = view.common.progress_session;
        auto& dressing_room = view.common.dressing_room;
        auto& challenge_log = view.common.challenge_log;
        auto& map_session = view.common.map_session;
        auto& story_targets = view.common.story_targets;
        auto& music = view.common.music;
        auto& cutscenes = view.common.cutscenes;
        auto& movie_bridge = view.common.movie_bridge;
        auto& mesh_buffers = view.common.mesh_buffers;
        auto& gameplay = view.gameplay;
        auto& castle = view.castle;
        auto& entry = view.entry;
        auto& localization = view.localization;
        auto& native_movie = view.native_movie;
        Json result{{"flags", Json::parse(source_flags.snapshot())},
                    {"localization", localization.snapshot()},
                    {"started", gameplay.started},
                    {"native_before", view.native_before},
                    {"native_now", kh02_native_flags()},
                    {"script", path(gameplay.script.Get())},
                    {"gimmick_script", path(castle.gimmick_script.Get())},
                    {"levels", Json::array()},
                    {"gears", Json::array()},
                    {"camera_matinees", Json::array()},
                    {"compatibility", view.compatibility}};
        result["native_unchanged"] = result["native_now"] == view.native_before;
        result["hud"] = Json::parse(hud.snapshot());
        result["challenge_tracking"] = Json::parse(challenge_tracker.snapshot());
        result["challenge_tracking"]["chest_error"] = chest_observer.error();
        result["challenge_tracking"]["chest_events"] = chest_observer.snapshot();
        result["challenge_counts"] = view.within_challenge_counts;
        result["challenge_states"] = view.within_challenges;
        result["challenge_persistence_error"] = progress_session.error();
        auto binding = reconciliation::view();
        result["challenge_binding"] = {
            {"active", view.challenges_active},        {"ready", binding->kh02_progress_ready},
            {"revision", progress_session.revision()}, {"live_revision", binding->kh02_progress_revision},
            {"error", binding->kh02_progress_error},   {"pawn", path(gameplay.pawn.Get())}};
        result["dressing_room"] = Json::parse(dressing_room.snapshot());
        result["challenge_log"] = Json::parse(challenge_log.snapshot());
        result["enemy_stats"] = Json::parse(enemy_stats.snapshot());
        result["compatibility"]["counter_presentation"] = result["hud"]["counter_visible"];
        result["entry"] = {{"phase", entry.phase_name()},
                           {"error", entry.failure()},
                           {"initialized", entry.initialized},
                           {"loading_held", castle.loading_held},
                           {"proof", castle.proof}};
        result["arrival"] = map_session.snapshot();
        result["story_targets"] = story_targets.snapshot();
        result["music"] = music.snapshot();
        result["collapse"] = cutscenes.collapse_snapshot();
        result["fade_layers"] = Json::array();
        auto* fade_library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        for (const int layer : {0, 1})
        {
            ReflectedCall amount(fade_library, L"BPTresGetFadeAmount");
            amount.object(L"WorldContextObject", pawn);
            amount.integer(L"InLayer", layer);
            amount.run();
            ReflectedCall changing(fade_library, L"BPTresIsFadeChange");
            changing.object(L"WorldContextObject", pawn);
            changing.integer(L"InLayer", layer);
            changing.run();
            auto* value = amount.param(L"ReturnValue");
            if (!value || value->GetElementSize() != sizeof(float) || value->GetClass().GetName() != L"FloatProperty")
            {
                throw std::runtime_error("kh02_diagnostic_fade_amount_layout");
            }
            result["fade_layers"].push_back(
                {{"layer", layer},
                 {"amount", *value->ContainerPtrToValuePtr<float>(amount.parameters.data())},
                 {"changing", changing.boolean_result()}});
        }
        result["event_aqua"] = cutscenes.event_aqua_snapshot();
        auto* current_world = static_cast<AActor*>(pawn)->GetWorld();
        result["ground_levels"] = kh02_ground_streams(current_world);
        result["camera_levels"] = kh02_stream_states(current_world, L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_");
        result["encounter_levels"] = kh02_stream_states(current_world, L"/Game/Zevels/dw/dw_02/umap/dw_02_enemy_");
        result["encounters"] = view.encounter_proof;
        result["encounters"]["postboss_observed"] = view.postboss_observed;
        result["encounters"]["postboss_maximum"] = view.postboss_maximum;
        result["encounters"]["travel_requested"] = view.story_travel_requested;
        result["reconstruction_actors"] = Json::array();
        for (const wchar_t letter : {L'B', L'C', L'D', L'E'})
        {
            const auto name = L"f_cd_Pro_GroundCollapse" + std::wstring(1, letter) + L"_Event";
            const auto class_path = L"/Game/Zlueprints/EventPawn/dw/" + name + L"." + name + L"_C";
            auto* cls = find_loaded_runtime_asset_class(class_path.c_str());
            if (!is_uobject_valid_fast(cls))
            {
                continue;
            }
            for (auto* actor : find_live_actors_of_class(pawn, cls, 8))
            {
                auto* mesh = try_read_reflected_object_property_value(actor, L"MyMesh");
                result["reconstruction_actors"].push_back(
                    {{"actor", path(actor)},
                     {"hidden", flag(actor, L"bHidden")},
                     {"mesh", path(try_read_reflected_object_property_value(mesh, L"SkeletalMesh"))},
                     {"mesh_visible", is_uobject_valid_fast(mesh) && flag(mesh, L"bVisible")}});
            }
        }
        auto* video_library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        ReflectedCall playing(video_library, L"TresUI_IsPlayingVideo");
        playing.run();
        ReflectedCall empty(video_library, L"TresUI_IsEmptyVideoSlot");
        empty.integer(L"SlotNo", 0);
        empty.run();
        result["movie"] = {{"playing", playing.boolean_result()},
                           {"slot0_empty", empty.boolean_result()},
                           {"background_observing", native_movie.observing()},
                           {"background_samples", native_movie.samples()},
                           {"calls", view.movie_calls},
                           {"state", native_movie.state()},
                           {"events", native_movie.events()}};
        result["movie_bridge"] = movie_bridge.snapshot();
        if (!view.native_movie_fixture && result["movie_bridge"]["phase"] != "idle" &&
            result["movie_bridge"]["phase"] != "stopped")
        {
            result["movie"]["playing"] = result["movie_bridge"]["playing"];
            result["movie"]["state"]["prepared"] = result["movie_bridge"]["ready"];
        }
        result["cpu_mesh_buffers"] = {{"previous", mesh_buffers.previous().value_or(-1)},
                                      {"current", mesh_buffers.current(pawn)}};
        for (auto& weak : castle.streams)
        {
            if (auto* s = weak.Get(); is_uobject_valid_fast(s))
            {
                ReflectedCall loaded(s, L"IsLevelLoaded");
                ReflectedCall visible(s, L"IsLevelVisible");
                ReflectedCall pending(s, L"IsStreamingStatePending");
                loaded.run();
                visible.run();
                pending.run();
                result["levels"].push_back({{"stream", path(s)},
                                            {"loaded", loaded.boolean_result()},
                                            {"visible", visible.boolean_result()},
                                            {"pending", pending.boolean_result()}});
            }
        }
        if (auto* cls = find_loaded_runtime_asset_class(L"/Game/Zlueprints/Gimmick/dw/g_dw_Gear/g_dw_Gear.g_dw_Gear_C"))
        {
            for (auto* a : find_live_actors_of_class(pawn, cls, 16))
            {
                ReflectedCall location(a, L"K2_GetActorLocation");
                location.run();
                auto* p = location.param(L"ReturnValue");
                auto* v = p->ContainerPtrToValuePtr<float>(location.parameters.data());
                result["gears"].push_back(
                    {{"actor", path(a)}, {"location", {v[0], v[1], v[2]}}, {"hidden", flag(a, L"bHidden")}});
                auto* hits = try_read_reflected_object_property_value(a, L"TresPlanHitCount");
                auto* count = is_uobject_valid_fast(hits) ? hits->GetPropertyByNameInChain(L"HitCount") : nullptr;
                if (count && count->GetClass().GetName() == L"IntProperty" && count->GetElementSize() == 4)
                {
                    result["gears"].back()["remaining_hits"] = *count->ContainerPtrToValuePtr<int32_t>(hits);
                }
            }
        }
        auto* matinee_class =
            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.MatineeActor");
        if (is_uobject_valid_fast(matinee_class))
        {
            for (auto* a : find_live_actors_of_class(pawn, matinee_class, 128))
            {
                const auto name = a->GetPathName();
                if (!name.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms030.") &&
                    !name.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms040.") &&
                    !name.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw013."))
                {
                    continue;
                }
                result["camera_matinees"].push_back({{"actor", path(a)},
                                                     {"playing", flag(a, L"bIsPlaying")},
                                                     {"paused", flag(a, L"bPaused")},
                                                     {"position", real(a, L"InterpPosition")}});
            }
        }
        return result;
    }

    auto Kh02StoryDiagnostics::opening_snapshot(const Kh02OpeningDiagnosticsView& view) -> Json
    {
        auto& opening_scene = view.opening_scene;
        auto& event_aqua = view.event_aqua;
        auto& entry = view.entry;
        auto& intro_presentation = view.intro_presentation;
        auto& opening_followup = view.opening_followup;
        Json result{{"stream", path(opening_scene.source_stream())},
                    {"aqua", path(event_aqua.actor())},
                    {"script", path(opening_scene.source_script())},
                    {"matinee", path(opening_scene.source_matinee())},
                    {"dispatched", opening_scene.was_dispatched()},
                    {"original_cutscene", "dw02_dw009"},
                    {"entry_enabled", entry.enabled},
                    {"first_castle_intro", entry.first_castle_intro},
                    {"intro_hud_hidden", intro_presentation.hud_hidden()},
                    {"entry_phase", entry.phase_name()},
                    {"entry_error", entry.failure()},
                    {"entry_observed_playing", entry.observed_playing()},
                    {"entry_maximum_position", entry.maximum_position()},
                    {"opening_followup_observed", opening_followup.state().observed()},
                    {"opening_followup_maximum", opening_followup.state().maximum()},
                    {"opening_handoff_ready", opening_followup.state().handoff_ready()},
                    {"opening_followup_script", path(opening_followup.source_script())},
                    {"view_cleanup", intro_presentation.cleanup_snapshot()}};
        if (auto* s = opening_scene.source_stream(); is_uobject_valid_fast(s))
        {
            for (auto name : {L"IsLevelLoaded", L"IsLevelVisible", L"IsStreamingStatePending"})
            {
                ReflectedCall c(s, name);
                c.run();
                result[to_utf8(name)] = c.boolean_result();
            }
        }
        if (auto* m = opening_scene.source_matinee(); is_uobject_valid_fast(m))
        {
            result["playing"] = flag(m, L"bIsPlaying");
            result["position"] = real(m, L"InterpPosition");
            auto* data = try_read_reflected_object_property_value(m, L"MatineeData");
            result["length"] = real(data, L"InterpLength");
            result["aqua_bound"] = *kh02_matinee_actor_slot(m) == event_aqua.actor();
        }
        if (auto* a = event_aqua.actor(); is_uobject_valid_fast(a))
        {
            auto* mesh = try_read_reflected_object_property_value(a, L"MyMesh");
            result["skeletal_mesh"] = path(try_read_reflected_object_property_value(mesh, L"SkeletalMesh"));
            ReflectedCall material(mesh, L"GetMaterial");
            material.integer(L"ElementIndex", 1);
            material.run();
            result["eye_lens_parent"] = path(try_read_reflected_object_property_value(material.result(), L"Parent"));
            auto** lens_cache = kh02_native_material_slot(mesh, 1);
            result["eye_lens_cache_matches"] = lens_cache && *lens_cache == material.result();
            ReflectedCall wet(mesh, L"GetMaterial");
            wet.integer(L"ElementIndex", 3);
            wet.run();
            result["eye_wetline_parent"] = path(try_read_reflected_object_property_value(wet.result(), L"Parent"));
            auto** wet_cache = kh02_native_material_slot(mesh, 3);
            result["eye_wetline_cache_matches"] = wet_cache && *wet_cache == wet.result();
            ReflectedCall lod(mesh, L"BP_GetPredictedLODLevel");
            lod.run();
            result["predicted_lod"] =
                read_function_int_param(lod.function, lod.parameters.data(), L"ReturnValue").value_or(-1);
            ReflectedCall location(a, L"K2_GetActorLocation");
            location.run();
            auto* p = location.param(L"ReturnValue");
            if (p && p->GetElementSize() == native_vector_bytes)
            {
                auto* v = p->ContainerPtrToValuePtr<float>(location.parameters.data());
                result["aqua_location"] = {v[0], v[1], v[2]};
            }
        }
        return result;
    }

    auto Kh02StoryDiagnostics::within_snapshot(UObject* pawn, const Kh02WithinDiagnosticsView& view) -> Json
    {
        auto& source_flags = view.common.source_flags;
        auto& hud = view.common.hud;
        auto& enemy_stats = view.common.enemy_stats;
        auto& challenge_tracker = view.common.challenge_tracker;
        auto& chest_observer = view.common.chest_observer;
        auto& progress_session = view.common.progress_session;
        auto& dressing_room = view.common.dressing_room;
        auto& challenge_log = view.common.challenge_log;
        auto& map_session = view.common.map_session;
        auto& story_targets = view.common.story_targets;
        auto& music = view.common.music;
        auto& cutscenes = view.common.cutscenes;
        auto& movie_bridge = view.common.movie_bridge;
        auto& mesh_buffers = view.common.mesh_buffers;
        auto& mirror = view.mirror;
        auto& information = view.information;
        auto& island_materials = view.island_materials;
        Json out{{"initialized", mirror.initialized},
                 {"information", {{"pending", information.pending()}, {"events", information.events()}}},
                 {"initialization_pending", is_uobject_valid_fast(mirror.initializer.Get())},
                 {"intro_started", mirror.intro_started},
                 {"error", mirror.error},
                 {"world", path(mirror.world.Get())},
                 {"flags", Json::parse(source_flags.snapshot())},
                 {"compatibility_error", view.compat_error},
                 {"compatibility_warning", view.compat_warning},
                 {"streams", kh02_stream_states(kh02_actor_world(pawn), L"/Game/Z")},
                 {"actors", Json::array()},
                 {"enemy_tables", Json::array()}};
        out["hud"] = Json::parse(hud.snapshot());
        out["enemy_stats"] = Json::parse(enemy_stats.snapshot());
        out["movie_bridge"] = movie_bridge.snapshot();
        out["cpu_mesh_buffers"] = {{"previous", mesh_buffers.previous().value_or(-1)},
                                   {"current", mesh_buffers.current(pawn)}};
        out["ending_presentation"] = cutscenes.presentation_snapshot();
        out["challenge_log"] = Json::parse(challenge_log.snapshot());
        out["challenge_tracking"] = Json::parse(challenge_tracker.snapshot());
        out["challenge_tracking"]["chest_error"] = chest_observer.error();
        out["challenge_tracking"]["chest_events"] = chest_observer.snapshot();
        out["mapset"] = {{"calls", view.mapset_calls}, {"last", view.last_mapset}};
        out["forest_story"] = mirror.forest_story_proof;
        if (!mirror.forest_visibility.events().empty())
        {
            out["forest_story"]["lod_reconciliations"] = mirror.forest_visibility.events();
        }
        out["dressing_room"] = Json::parse(dressing_room.snapshot());
        out["arrival"] = map_session.snapshot();
        out["story_targets"] = story_targets.snapshot();
        out["event_aqua"] = cutscenes.event_aqua_snapshot();
        out["music"] = music.snapshot();
        if (is_uobject_valid_fast(pawn))
        {
            ReflectedCall battle(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"TresIsSpecialBattleMode");
            battle.run();
            out["special_battle_mode_active"] = battle.boolean_result();
            for (const auto* method : {L"GetHitPoint", L"GetStateID"})
            {
                ReflectedCall query(pawn, method);
                query.run();
                out["hero"][to_utf8(method)] =
                    read_function_int_param(query.function, query.parameters.data(), L"ReturnValue").value_or(-1);
            }
            out["hero"]["pawn"] = path(pawn);
        }
        const auto progress = reconciliation::view();
        out["persistence"] = {
            {"ready", progress->kh02_progress_ready},
            {"revision", progress_session.revision()},
            {"loaded_slot", progress->loaded_save_slot},
            {"restored", progress_session.restored()},
            {"error", progress_session.error().empty() ? progress->kh02_progress_error : progress_session.error()},
            {"working", kh02_progress::to_json(progress->kh02_progress)}};
        out["unavailable_destination_reactions_disabled"] = mirror.unsupported_reactions_disabled;
        for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
        {
            const auto p = actor->GetPathName();
            const auto name = actor->GetName();
            if (p.starts_with(L"/Game/Zevels/dw/dw_10/umap/dw_10_enemy.") && name.starts_with(L"e_dw401_Pawn_C"))
            {
                Json boss{{"actor", path(actor)},
                          {"core_visible", flag(actor, L"m_bCoreDisp")},
                          {"player_attack_floor", flag(actor, L"m_bIsHpLimitOneExceptPlayerAttack")},
                          {"unconditional_floor", flag(actor, L"m_bIsHpLimitOne")}};
                ReflectedCall hp(actor, L"GetHitPoint");
                hp.run();
                boss["hp"] = read_function_int_param(hp.function, hp.parameters.data(), L"ReturnValue").value_or(-1);
                ReflectedCall ride(actor, L"IsMickeyRide");
                ride.run();
                boss["mickey_ride"] = ride.boolean_result();
                ReflectedCall core(actor, L"GetCoreMeshLoc");
                core.run();
                auto* value = core.param(L"ReturnValue");
                if (value && value->GetElementSize() == native_vector_bytes)
                {
                    auto* v = value->ContainerPtrToValuePtr<float>(core.parameters.data());
                    boss["core_location"] = {v[0], v[1], v[2]};
                }
                if (auto* mesh = try_read_reflected_object_property_value(actor, L"MyMesh");
                    is_uobject_valid_fast(mesh))
                {
                    for (const auto* socket : {L"center", L"vfx_center"})
                    {
                        ReflectedCall position(mesh, L"GetSocketLocation");
                        position.name(L"InSocketName", socket);
                        position.run();
                        auto* result = position.param(L"ReturnValue");
                        if (result && result->GetElementSize() == native_vector_bytes)
                        {
                            auto* v = result->ContainerPtrToValuePtr<float>(position.parameters.data());
                            boss[std::string("socket_") + to_utf8(socket)] = {v[0], v[1], v[2]};
                        }
                    }
                }
                out["ending_boss"] = std::move(boss);
            }
            if (!p.starts_with(L"/Game/Zevels/dw/") ||
                (!name.starts_with(L"g_dw_Mirror") && !name.starts_with(L"g_dw_ColumnMirror") &&
                 !name.starts_with(L"g_dw_ThreeFlower") && !name.starts_with(L"g_ex_SavePoint") &&
                 !name.starts_with(L"lv_") && !name.starts_with(L"TresMapJump")))
            {
                continue;
            }
            ReflectedCall loc(actor, L"K2_GetActorLocation");
            loc.run();
            auto* v = loc.param(L"ReturnValue");
            Json row{
                {"actor", path(actor)}, {"class", path(actor->GetClassPrivate())}, {"hidden", flag(actor, L"bHidden")}};
            row["collision"] = flag(actor, L"bActorEnableCollision");
            if (actor->GetPropertyByNameInChain(L"Enable"))
            {
                row["enabled"] = flag(actor, L"Enable");
            }
            if (auto* component = try_read_reflected_object_property_value(actor, L"CollisionComponent");
                is_uobject_valid_fast(component))
            {
                row["overlap_events"] = flag(component, L"bGenerateOverlapEvents");
                for (const auto* property : {L"RelativeLocation", L"RelativeScale3D", L"BoxExtent"})
                {
                    if (auto* value = component->GetPropertyByNameInChain(property);
                        value && value->GetElementSize() == native_vector_bytes)
                    {
                        auto* v = value->ContainerPtrToValuePtr<float>(component);
                        row[to_utf8(property)] = {v[0], v[1], v[2]};
                    }
                }
            }

            if (v && v->GetElementSize() == native_vector_bytes)
            {
                auto* f = v->ContainerPtrToValuePtr<float>(loc.parameters.data());
                row["position"] = {f[0], f[1], f[2]};
            }
            ReflectedCall rotation(actor, L"K2_GetActorRotation");
            rotation.run();
            auto* rot = rotation.param(L"ReturnValue");
            if (rot && rot->GetElementSize() == native_vector_bytes)
            {
                auto* f = rot->ContainerPtrToValuePtr<float>(rotation.parameters.data());
                row["rotation"] = {f[0], f[1], f[2]};
            }
            if (name.starts_with(L"lv_LODTriggerVolume"))
            {
                ReflectedCall overlap(actor, L"IsOverlappingActor");
                overlap.object(L"Other", pawn);
                overlap.run();
                row["overlaps_player"] = overlap.boolean_result();
                ReflectedCall player(pawn, L"SqEX_IsObjTypePlayer");
                player.run();
                row["player_object_type"] = player.boolean_result();
                if (auto* brush = try_read_reflected_object_property_value(actor, L"BrushComponent");
                    is_uobject_valid_fast(brush))
                {
                    row["brush"] = path(brush);
                    row["overlap_events"] = flag(brush, L"bGenerateOverlapEvents");
                }
                if (auto* property = actor->GetPropertyByNameInChain(L"LODGimmicks");
                    property && property->IsA<FArrayProperty>())
                {
                    auto* values = property->ContainerPtrToValuePtr<TArray<UObject*>>(actor);
                    row["lod_gimmicks"] = Json::array();
                    for (auto* gimmick : *values)
                    {
                        if (!is_uobject_valid_fast(gimmick))
                        {
                            continue;
                        }
                        auto* mesh = try_read_reflected_object_property_value(gimmick, L"MyMesh");
                        row["lod_gimmicks"].push_back(
                            {{"actor", path(gimmick)},
                             {"shown", flag(gimmick, L"temp_IsShowLODGimmick")},
                             {"started", flag(gimmick, L"temp_IsStartedGimmick")},
                             {"mesh_visible", is_uobject_valid_fast(mesh) && flag(mesh, L"bVisible")}});
                    }
                }
            }
            auto* reactor = try_read_reflected_object_property_value(actor, L"TresReactor");
            if (!is_uobject_valid_fast(reactor))
            {
                reactor = try_read_reflected_object_property_value(actor, L"MyReactor");
            }
            if (is_uobject_valid_fast(reactor))
            {
                row["reactor"] = path(reactor);
                for (auto* prop : reactor->GetClassPrivate()->ForEachProperty())
                {
                    const auto n = prop->GetName();
                    if (n != L"m_Command" && n != L"m_bIsGimmickActive" && n.find(L"Enable") == std::wstring::npos)
                    {
                        continue;
                    }
                    if (prop->IsA<FBoolProperty>())
                    {
                        row[to_utf8(n)] = static_cast<FBoolProperty*>(prop)->GetPropertyValueInContainer(reactor);
                    }
                    else if (prop->GetElementSize() == 1 && (prop->GetClass().GetName() == L"EnumProperty" ||
                                                             prop->GetClass().GetName() == L"ByteProperty"))
                    {
                        row[to_utf8(n)] = *prop->ContainerPtrToValuePtr<uint8_t>(reactor);
                    }
                }
            }
            out["actors"].push_back(std::move(row));
        }
        std::vector<UObject*> tables;
        UObjectGlobals::FindAllOf(L"TresChrDataTableSet", tables);
        for (auto* table : tables)
        {
            if (is_uobject_valid_fast(table) && table->GetPathName().starts_with(L"/Game/Zlueprints/Enemy/"))
            {
                auto* p = table->GetPropertyByNameInChain(L"m_DataType");
                if (p && p->GetElementSize() == 1)
                {
                    out["enemy_tables"].push_back(
                        {{"asset", path(table)}, {"type", *p->ContainerPtrToValuePtr<uint8_t>(table)}});
                }
            }
        }
        out["entrance_mirror"] = view.encounter_proof.value("entrance_mirror", Json{});
        out["horizon_material"] = path(mirror.horizon.material());
        out["island_material_slots"] = island_materials.adapted_slots();
        return out;
    }
} // namespace kh3ap
