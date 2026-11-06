#include "kh02_postboss_handoff.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include "kh3ap/features/kh02/progress/kh02_aqua_progression.h"
#include "kh3ap/features/kh02/combat/kh02_phantom_equipment.h"
#include "kh02_session_retirement.h"
#include "kh02_story_overrides.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/kh02/diagnostics/kh02_diagnostic_actions.h"
#include "kh3ap/features/enemies/sora_experience_test.h"
#endif
#include "kh3ap/features/kh02/diagnostics/kh02_story_diagnostics.h"
#include "kh02_story_event_router.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/kh02/diagnostics/kh02_mirror_transition_diagnostics.h"
#endif
#include "kh02_mirror_travel.h"
#include "kh02_castle_gameplay_session.h"
#include "kh02_mirror_gameplay_session.h"
#include "kh3ap/features/kh02/cutscenes/kh02_opening_scene.h"
#include "kh3ap/features/kh02/progress/kh02_progress_session.h"
#include "kh3ap/features/kh02/ui/kh02_information_decoder.h"
#include "kh02_arrival_readiness.h"
#include "kh3ap/features/kh02/cutscenes/kh02_opening_followup.h"
#include "kh3ap/features/kh02/world/kh02_castle_save_notice.h"
#include "kh3ap/features/kh02/world/kh02_castle_entry_dispatch.h"
#include "kh02_source_level_readiness.h"
#include "kh3ap/features/kh02/world/kh02_mirror_horizon.h"
#include "kh3ap/features/kh02/cutscenes/kh02_event_aqua.h"
#include "kh3ap/features/kh02/cutscenes/kh02_encounter_scene_binding.h"
#include "kh3ap/features/kh02/cutscenes/kh02_scene_catalog.h"
#include "kh3ap/features/kh02/runtime/kh02_runtime_config.h"
#include "kh3ap/features/kh02/world/kh02_area_selection.h"
#include "kh3ap/features/kh02/cutscenes/kh02_collapse_preparation.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_presentation.h"
#include "kh02_mirror_arrival.h"
#include "kh02_forest_handoff.h"
#include "kh3ap/features/kh02/cutscenes/kh02_scene_hero_visibility.h"
#include "kh3ap/features/kh02/cutscenes/kh02_matinee_binding.h"
#include "kh3ap/features/kh02/world/kh02_entrance_mirror.h"
#include "kh3ap/features/kh02/cutscenes/kh02_intro_presentation.h"
#include "kh3ap/features/kh02/compatibility/kh02_aliases.h"
#include "kh3ap/features/kh02/compatibility/kh02_compatibility_enums.h"
#include "kh3ap/features/kh02/diagnostics/kh02_runtime_inspection.h"
#include "kh3ap/features/kh02/world/kh02_castle_boss_streams.h"
#include "kh3ap/features/kh02/world/kh02_castle_scenery.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_reconciliation.h"
#include "kh02_timed_inputs.h"
#include "kh3ap/features/kh02/runtime/kh02_entry_assets.h"
#include "kh02_help_continuations.h"
#include "kh3ap/features/kh02/world/kh02_doors.h"
#include "kh3ap/features/kh02/world/kh02_forest_vines.h"
#include "kh3ap/features/kh02/world/kh02_castle_path.h"
#include "kh3ap/features/kh02/world/kh02_island_materials.h"
#include "kh3ap/features/kh02/core/kh02_localization.h"
#include "kh3ap/features/kh02/world/kh02_shooting_star.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_observer.h"
#include "kh3ap/features/kh02/treasure/kh02_postclear_treasure.h"
#include "kh3ap/features/kh02/movies/kh02_movie_probe.h"
#include "kh02_story_targets.h"
#include "kh3ap/features/kh02/world/kh02_forest_visibility.h"
#include "kh3ap/features/kh02/ui/kh02_information_queue.h"
#include "kh3ap/features/kh02/core/kh02_mesh_buffer_lease.h"
#include "kh3ap/features/kh02/world/kh02_mapset.h"
#include "kh3ap/features/kh02/runtime/kh02_progress_service.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh02_entry_session.h"
#include "kh02_story_music.h"
#include "kh3ap/features/kh02/core/kh02_world_identity.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_ids.h"
#include "kh02_story.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include "kh02_islands_resume_policy.h"
#include "kh3ap/features/kh02/compatibility/kh02_compatibility_operands.h"
#include "kh3ap/features/kh02/movies/kh02_optional_audio.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_session.h"
#include "kh02_map_session.h"
#include "kh02_story_flags.h"
#include "kh3ap/features/kh02/ui/kh02_hud.h"
#include "kh3ap/features/kh02/ui/kh02_inventory_menu.h"
#include "kh3ap/features/kh02/rewards/kh02_ability_capacity.h"
#include "kh3ap/features/kh02/combat/kh02_enemy_stats.h"
#include "kh3ap/features/kh02/combat/kh02_darkside_projectile_repair.h"
#include "kh3ap/features/kh02/combat/kh02_darkside_pillar_repair.h"
#include "kh02/mirror_host_metadata.h"
#include "kh02/forest_runtime_metadata.h"
#include "kh02/ending_runtime_metadata.h"
#include "kh02/story_movie_metadata.h"
#include "kh02/runtime_metadata.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_defs.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_enum.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include "kh3ap/features/kh02/wardrobe/kh02_dressing_room.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_tracker.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_policy.h"
#include "kh02_loading_policy.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UEnum.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#include <nlohmann/json.hpp>
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>
#include <map>
#include <tuple>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr std::size_t native_array_header_bytes = 16;
        constexpr std::size_t native_string_bytes = 16;
        constexpr std::size_t native_vector_bytes = 3 * sizeof(float);
        constexpr int actor_scan_limit = 16'384;
        constexpr int class_actor_scan_limit = 256;
        constexpr int event_history_limit = 64;
        constexpr float scene_skip_music_fade_seconds = 1.0f;

        namespace asset_request_id
        {
            constexpr int castle_mapset = 0x4b483450;
            constexpr int resume_mapset_base = 0x4b483600;
        } // namespace asset_request_id

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        auto mirror_room(std::wstring_view world) -> const kh02mirror_metadata::Room*
        {
            return kh02_world_identity::mirror_room(world);
        }

        bool flag(UObject* o, const wchar_t* n)
        {
            auto* p = is_uobject_valid_fast(o) ? o->GetPropertyByNameInChain(n) : nullptr;
            if (!p || !p->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(o);
        }

        float real(UObject* o, const wchar_t* n)
        {
            auto* p = is_uobject_valid_fast(o) ? o->GetPropertyByNameInChain(n) : nullptr;
            if (!p || p->GetClass().GetName() != L"FloatProperty" || p->GetElementSize() != 4)
            {
                throw std::runtime_error("kh02_story_float");
            }
            return *p->ContainerPtrToValuePtr<float>(o);
        }

        void set_flag(UObject* o, const wchar_t* n, bool v)
        {
            if (!is_uobject_valid_fast(o) || !write_bool_function_param(o->GetPropertyByNameInChain(n), o, v))
            {
                throw std::runtime_error("kh02_story_set_flag");
            }
        }

        std::string path(UObject* o)
        {
            return is_uobject_valid_fast(o) ? to_utf8(o->GetPathName()) : "";
        }

    } // namespace

    struct Kh02Story::Impl
    {
        using Thunk = void (*)(UObject*, FFrame&, void*);
        static inline Impl* compat_instance{};

        explicit Impl(std::function<Kh02BattleLevelSettings()> settings) : enemy_stats(std::move(settings))
        {
            compat_instance = this;
            dressing_room.on_change(
                [this]
                {
                    const auto equipped = dressing_room.progress_selection().equipped;
                    challenge_tracker.wardrobe(
                        std::all_of(equipped.begin(), equipped.end(), [](const auto& item) { return item != 0; }));
                    if (std::any_of(equipped.begin(), equipped.end(), [](const auto& item) { return item != 0; }))
                    {
                        within_challenges[kh02_challenge_id::equip_accessorie_once] =
                            kh02_challenge_state::complete; // Equip a Wardrobe Piece.
                        within_challenge_counts[kh02_challenge_id::equip_accessorie_once] = 1;
                    }
                    publish_mirror_progress();
                });
            source_flags.on_changed(
                [this]
                {
                    story_targets.request_sync();
                    publish_mirror_progress();
                });
        }

        Kh02EntrySession entry;
        Kh02AquaProgression aqua_progression;
        Kh02GameplayBinding gameplay;
        Kh02CastleGameplaySession castle;
        Kh02MirrorGameplaySession mirror;

        Kh02StoryTargets story_targets;
        Kh02PhantomEquipment phantom_equipment;

        static void target_activated(UObject* component, FFrame& stack, void* result)
        {
            auto* self = compat_instance;
            self->native_overrides.original(Kh02StoryNativeOverrides::Slot::target_activate)(component, stack, result);
            // A script can reactivate a lock-on component without changing its
            // actor's visibility. Native Blueprint calls bypass ProcessEvent,
            // so observe the owning thunk and leave its operand parsing intact.
            if (self->event_thread.load(std::memory_order_acquire) == GetCurrentThreadId() &&
                is_uobject_valid_fast(component))
            {
                self->story_targets.request_sync(component->GetOuterPrivate());
            }
        }

        void begin_arrival(UObject* w)
        {
            enemy_stats.set_active(true);
            // Direct mirror/ending entry also loads source skeletal meshes.
            // DW033's Darkside disappearance samples their CPU triangle indices;
            // preserve those buffers before the source streams initialize them.
            map_session.observe_world(w);
            mesh_buffers.retain(w);
            configure_music(w);
            const auto name = w->GetPathName();
            const auto* room = mirror_room(name);
            if (name != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" &&
                name != L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin" &&
                (!room || room->area > 9 || room->area == 7))
            {
                return;
            }
            map_session.begin_arrival(w);
        }

        static bool loading_closed()
        {
            return Kh02MapSession::loading_closed();
        }

        auto retirement() -> Kh02SessionRetirement
        {
            return {map_session,
                    movie_bridge,
                    cutscenes,
                    help_callbacks,
                    information,
                    mirror_travel,
                    postboss,
                    mirror,
                    castle,
                    entry,
                    challenge_log,
                    dressing_room,
                    intro_presentation,
                    timed_inputs,
                    enemy_stats,
                    challenge_tracker,
                    hud,
                    story_targets,
                    island_materials,
                    chest_observer,
                    native_movie,
                    gameplay,
                    entry_world,
                    mesh_buffers,
                    entry_assets,
                    ability_capacity,
                    darkside_projectiles,
                    darkside_pillars};
        }

        Kh02MapSession map_session{
            {[this](UObject* world)
             {
                 return entry_world.Get() == world && entry.phase() == Kh02EntrySession::Phase::error ? entry.failure()
                                                                                                      : std::string{};
             },
             [this](UObject* world) -> Json
             {
                 auto* opening = opening_scene.source_matinee();
                 if (!opening_scene.was_dispatched() || !is_uobject_valid_fast(opening) ||
                     kh02_actor_world(opening) != world || !flag(opening, L"bIsPlaying") ||
                     real(opening, L"InterpPosition") <= 0.f)
                 {
                     return nullptr;
                 }
                 return {{"matinee", path(opening)}, {"position", real(opening, L"InterpPosition")}};
             },
             [this](uint64_t now) { return music.release_arrival_menu(now); },
             [this](UObject* pawn)
             {
                 Kh02InventoryMenu::install_shared();
                 auto* world = kh02_actor_world(pawn);
                 const auto* room = mirror_room(world->GetPathName());
                 if (room && (room->area == 8 || room->area == 9) && mirror_travel.arrival_package == room->package)
                 {
                     const auto tag = std::move(mirror_travel.arrival_tag);
                     mirror_travel.arrival_tag.clear();
                     mirror_travel.arrival_package.clear();
                     if (tag.starts_with(L"dw_08_Lv_Save_") || tag == L"dw_09_Lv_Save_01")
                     {
                         // Native room return can retain its previous arena
                         // position even when travel selected a save-point tag.
                         // Resolve this explicit request once its source actors
                         // are loaded, before restoring Aqua or releasing cover.
                         ReflectedCall before(pawn, L"K2_GetActorLocation");
                         before.run();
                         ReflectedCall warp(pawn, L"WarpToTaggedPlayerStart");
                         warp.name(L"InTag", tag.c_str());
                         warp.boolean(L"bResetCamera", true);
                         warp.boolean(L"bStateClear", true);
                         warp.run();
                         if (!warp.boolean_result())
                         {
                             throw std::runtime_error("kh02_resume_save_point_missing");
                         }
                         ReflectedCall after(pawn, L"K2_GetActorLocation");
                         after.run();
                         map_session.proof()["resume_spawn"] = {{"tag", to_utf8(tag)},
                                                                {"before", before.result_vector()},
                                                                {"after", after.result_vector()}};
                     }
                 }
                 if (!RuntimeGardenReturnService::finish_kh02_arrival(pawn))
                 {
                     return false;
                 }
                 dressing_room.tick(pawn, challenges_active());
                 if (!dressing_room.ready())
                 {
                     return false;
                 }
                 return true;
             },
             [this](UObject* world) { return entry.first_castle_intro && entry_world.Get() == world; },
             [this](UObject* world) { return entry.initialized && gameplay.world.Get() == world; },
             [this](UObject* context, UObject* w) -> Json
             { return kh02_sample_arrival_readiness(context, w, intro_presentation); },
             [this] { music.prepare_battle(); }, [this] { music.retire_opening(); },
             [this] { retirement().run(Kh02RetirementReason::SourceReplacement); },
             [this](UObject* world)
             {
                 auto* scene = postboss.matinee_binding().Get();
                 return is_uobject_valid_fast(scene) && kh02_actor_world(scene) == world &&
                        (flag(scene, L"bIsPlaying") || flag(scene, L"bPaused"));
             }}};
        Kh02PostBossHandoff postboss;
        Json encounter_proof = Json::object();
        Kh02IslandMaterials island_materials;

        Kh02MirrorTravel mirror_travel;
        Kh02TimedInputs timed_inputs;

        std::map<int, bool> challenge_events;

        Kh02StoryFlags source_flags;
        std::map<int, int> within_challenges;
        std::map<int, int> within_challenge_counts;
        Kh02ChallengeLog challenge_log;
        Kh02PostclearTreasure postclear_treasure;
        Kh02DressingRoom dressing_room;
        Kh02AbilityCapacity ability_capacity;
        Kh02ChallengeTracker challenge_tracker{within_challenges, within_challenge_counts};
        Kh02ProgressSession progress_session{source_flags,      dressing_room,           challenge_tracker,
                                             within_challenges, within_challenge_counts, challenge_events};
        bool within_resume_entry{};
        Kh02ChestObserver chest_observer;
        std::atomic<DWORD> event_thread{};

        static void source_chest_opened(UObject* chest) noexcept
        {
            auto* self = compat_instance;
            if (!self || self->event_thread.load(std::memory_order_acquire) != GetCurrentThreadId())
            {
                return;
            }
            self->chest_observer.notify(chest);
        }

        void publish_mirror_progress()
        {
            progress_session.publish();
        }

        void bind_mirror_progress(UObject* world, int checkpoint)
        {
            const auto decision = progress_session.bind(world, checkpoint, castle.backtrack);
            castle.backtrack = decision.backtrack;
            within_resume_entry = decision.resume;
        }

        Kh02InformationQueue information;

        static void source_information(UObject* object, FFrame& stack, void* result)
        {
            auto* self = compat_instance;
            if (!self || !self->native_overrides.original(Kh02StoryNativeOverrides::Slot::information))
            {
                return;
            }
            const auto message = kh02_decode_information(stack);
            if (!message)
            {
                self->native_overrides.original(Kh02StoryNativeOverrides::Slot::information)(object, stack, result);
                return;
            }
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            auto* world = message->world;
            // Source callbacks cannot reclaim ownership from a newer host.
            // A late old-world information event must not retire its movie.
            if (!self->map_session.owns_world(world))
            {
                frame->Code = message->next_code;
                return;
            }
            if (self->information.full())
            {
                self->native_overrides.original(Kh02StoryNativeOverrides::Slot::information)(object, stack, result);
                return;
            }
            self->information.enqueue(world, self->map_session.generation(), message->schema, message->text,
                                      message->caller, message->auto_close);
            frame->Code = message->next_code;
        }

        void tick_information(UObject* pawn, UObject* world)
        {
            if (information.pending() == 0)
            {
                return;
            }
            const bool can_show = !map_session.active() && loading_closed() &&
                                  !(entry.first_castle_intro && entry.gameplay && !entry.done()) &&
                                  !flag(pawn, L"bHidden");
            information.tick(world, map_session, can_show);
        }

        auto encounter_services() -> Kh02EncounterSceneBinding::Services
        {
            return {gameplay.world,   gameplay.pawn, event_aqua,      cutscenes,
                    opening_followup, postboss,      encounter_proof, compat_error};
        }

        static void encounter_play(UObject* object, FFrame& stack, void* result)
        {
            auto* self = compat_instance;
            if (!self || !self->native_overrides.original(Kh02StoryNativeOverrides::Slot::play))
            {
                return;
            }
            Kh02EncounterSceneBinding::prepare_and_bind(object, self->encounter_services());
            self->native_overrides.original(Kh02StoryNativeOverrides::Slot::play)(object, stack, result);
        }

        static const char* full_movie_for_scene(UObject* matinee)
        {
            return kh02_scene_full_movie(matinee->GetPathName());
        }

        Kh02EntryAssets entry_assets;
        bool entry_asset_completion{};
        uint64_t entry_asset_callbacks{};

        static void entry_asset_loaded(UObject* context, FFrame&, void*)
        {
            auto* self = compat_instance;
            if (self && self->event_thread.load(std::memory_order_acquire) == GetCurrentThreadId() &&
                self->castle.armed && !self->entry.initialized && self->castle.preload_pawn.Get() == context)
            {
                self->entry_asset_completion = true;
                ++self->entry_asset_callbacks;
            }
        }

        static void arrival_level_changed(UObject* stream, FFrame&, void*)
        {
            auto* self = compat_instance;
            if (self && self->event_thread.load(std::memory_order_acquire) == GetCurrentThreadId() &&
                is_uobject_valid_fast(stream) && self->map_session.owns_world(stream->GetOuterPrivate()))
            {
                self->map_session.notify_readiness();
            }
        }

        ~Impl()
        {
            map_session.cancel();
            cutscenes.cancel();
            set_native_chest_open_observer(nullptr);
            native_overrides.restore();
            ability_capacity.release();
            entry_assets.release();
            if (compat_instance == this)
            {
                compat_instance = nullptr;
            }
        }

        static void levels_shown(UObject* stream, FFrame& stack, void*)
        {
            auto* self = compat_instance;
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (frame->Code)
            {
                ++frame->Code;
            }
            if (!self || self->entry.initialized || self->castle.armed)
            {
                return;
            }
            bool owned = false;
            for (const auto& item : self->castle.streams)
            {
                owned |= item.Get() == stream;
            }
            if (!owned)
            {
                return;
            }
            try
            {
                self->finish_entry_levels();
            }
            catch (const std::exception& e)
            {
                self->entry.fail(e.what());
                self->castle.loading_held = false;
            }
        }

        void initialize_castle_host(UObject* object)
        {
            if (!reconciliation::view()->kh02_progress_ready)
            {
                return;
            }
            castle.initializer = {};
            const bool native_restore = !castle.armed;
            // Preserve a failed bootstrap on this world. The frame
            // update must not replace it with another 90-second wait.
            entry_world = make_uobject_weak(kh02_actor_world(object));
            try
            {
                auto* world = kh02_actor_world(object);
                if (!enemy_stats.prepare(object))
                {
                    throw std::runtime_error("kh02_enemy_stats_not_preloaded");
                }
                castle.backtrack = castle.backtrack_pending;
                castle.backtrack_pending = false;
                bind_mirror_progress(world, kh02_checkpoint::gear_scene_dw011);
                entry.first_castle_intro = !castle.backtrack;
                if (entry.first_castle_intro)
                {
                    auto flags = source_flags.values();
                    flags["gameflow_DW"] = kh02_checkpoint::gear_scene_dw009;
                    source_flags.restore_mirror(flags);
                }
                opening_followup.reset();
                begin_arrival(world);
                entry.reset_observation();
                // Portal/Area Select re-entry must resume an unfinished tower
                // battle just like a native save restore. The ordinary field
                // dispatcher restores scenery but does not recreate this fight.
                const bool retry = source_flags.value(L"gameflow_DW") == kh02_checkpoint::castle_tower_start &&
                                   source_flags.value(L"gameflow_DW_GEAR_GETCOUNT") == kh02_gear_counter::all_collected;
                castle.retry = retry;
                if (native_restore)
                {
                    castle.proof = {{"native_save_restore", true}};
                }
                hud.reset_world();
                counter_requested = false;
                gameplay.pawn = {};
                event_aqua.reset();
                cutscenes.cancel();
                help_callbacks.clear();
                entry_world = make_uobject_weak(world);
                castle.begin(world, entry, gameplay);
            }
            catch (const std::exception& e)
            {
                castle.proof["bootstrap_error"] = e.what();
                entry.fail(e.what());
            }
        }

        void host_begin_play(UObject* object)
        {
            if (object->GetClassPrivate()->GetName() == L"APKH02WorldWithin_C" &&
                object->GetPathName().starts_with(L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin."))
            {
                try
                {
                    auto* w = kh02_actor_world(object);
                    begin_arrival(w);
                    bind_mirror_progress(w, kh02_checkpoint::mirror_scene_dw015);
                    mirror.begin_hub(w, mirror_services());
                }
                catch (const std::exception& e)
                {
                    mirror.error = e.what();
                }
                return;
            }
            auto* candidate_world = kh02_actor_world(object);
            if (auto* r =
                    is_uobject_valid_fast(candidate_world) ? mirror_room(candidate_world->GetPathName()) : nullptr;
                r && object->GetClassPrivate()->GetName() == std::wstring(r->name) + L"_C")
            {
                try
                {
                    auto* w = kh02_actor_world(object);
                    bind_mirror_progress(w, kh02_checkpoint::mirror_scene_dw016);
                    const int source_checkpoint = source_flags.value(L"gameflow_DW");
                    const int area_checkpoint = kh02_area_entry_checkpoint(r->area, source_checkpoint);
                    if (area_checkpoint != source_checkpoint)
                    {
                        auto flags = source_flags.values();
                        flags["gameflow_DW"] = area_checkpoint;
                        source_flags.restore_mirror(flags);
                    }
                    if (w->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands")
                    {
                        const int checkpoint = source_flags.value(L"gameflow_DW");
                        const int entry_checkpoint = kh02_islands_entry_checkpoint(checkpoint);
                        if (entry_checkpoint != checkpoint)
                        {
                            auto flags = source_flags.values();
                            flags["gameflow_DW"] = entry_checkpoint;
                            source_flags.restore_mirror(flags);
                        }
                    }
                    begin_arrival(w);
                    if (r->area >= 4 && r->area <= 6)
                    {
                        source_flags.select_mirror_room_entry(r->area);
                    }
                    mirror.begin_room(w, object, mirror_services());
                }
                catch (const std::exception& e)
                {
                    mirror.error = e.what();
                }
                return;
            }
            if (object->GetClassPrivate()->GetName() != L"APKH02CastleTown_C" ||
                !object->GetPathName().starts_with(L"/Game/Maps/APKH02CastleTown/APKH02CastleTown."))
            {
                return;
            }
            castle.initializer = make_uobject_weak(object);
            entry.initialized = false;
            entry.await_saved_progress();
            initialize_castle_host(object);
        }

        bool prepare_entry(UObject* pawn)
        {
            entry_asset_completion = false;
            RuntimeAssetLoadCallbackScope completion(L"KH02EntryAssetLoaded");
            const auto config_file = kh02_read_runtime_config(L"kh02_story_config.json", kh02_entry_config_limit_bytes,
                                                              true, "kh02_entry_image", "kh02_entry_config_size");
            if (!config_file)
            {
                return true;
            }
            const auto& config = *config_file;
            if (!config.value("prepare_gameplay_on_entry", false))
            {
                return true;
            }
            if (!is_uobject_valid_fast(pawn))
            {
                throw std::runtime_error("kh02_entry_context");
            }
            const auto progress = reconciliation::view();
            if (!progress->kh02_progress_ready)
            {
                const auto reason = !progress->error.empty() ? progress->error : progress->kh02_progress_error;
                throw std::runtime_error(reason.empty() ? "kh02_entry_progress_not_ready_connect_client"
                                                        : "kh02_entry_progress_not_ready:" + reason);
            }
            mark_kh02_provider_content_requested();
            if (castle.preload_pawn.Get() != pawn || !castle.armed)
            {
                Json setup_timings = Json::object();
                auto step_started = std::chrono::steady_clock::now();
                const auto record_setup = [&](const char* name)
                {
                    const auto now = std::chrono::steady_clock::now();
                    setup_timings[name] =
                        std::chrono::duration_cast<std::chrono::microseconds>(now - step_started).count();
                    step_started = now;
                };
                castle.preload_pawn = make_uobject_weak(pawn);
                mesh_buffers.retain(pawn);
                record_setup("mesh_buffers_us");
                localization.prepare();
                record_setup("localization_us");
                install_compat();
                record_setup("compatibility_us");
                native_before = kh02_native_flags();
                record_setup("native_flags_us");
                castle.armed = true;
                castle.loading_held = false;
                entry.initialized = false;
                castle.mapset_requested = false;
                castle.retry = false;
                event_aqua.reset();
                gameplay.pawn = {};
                cutscenes.cancel();
                castle.proof = {{"armed_before_travel", true},
                                {"host_begin_play", false},
                                {"setup_timings_us", setup_timings},
                                {"preload_started_at_ms", GetTickCount64()}};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                auto* fade_library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
                Json fade_bindings = Json::object();
                for (const auto* method : {L"BPTresStartFadeIn", L"WaitBPTresStartFadeIn", L"WaitBPTresCancelFade"})
                {
                    auto* function = fade_library->GetFunctionByNameInChain(method);
                    if (!is_uobject_valid_fast(function))
                    {
                        continue;
                    }
                    Json fields = Json::array();
                    for (auto* property : function->ForEachProperty())
                    {
                        if (property->HasAnyPropertyFlags(CPF_Parm))
                        {
                            fields.push_back({{"name", to_utf8(property->GetName())},
                                              {"type", to_utf8(property->GetClass().GetName())},
                                              {"offset", property->GetOffset_Internal()},
                                              {"bytes", property->GetElementSize()}});
                        }
                    }
                    fade_bindings[to_utf8(method)] = {{"bytes", function->GetParmsSize()}, {"fields", fields}};
                }
                castle.proof["fade_completion_bindings"] = std::move(fade_bindings);
#endif
            }
            const bool aqua_ready = event_aqua.preload(pawn, entry_assets);
            constexpr auto mapset = L"/Game/Zevels/dw/dw_02/mapset/ms_dw_02.ms_dw_02";
            auto* mapset_asset = find_loaded_runtime_asset(mapset);
            if (!mapset_asset)
            {
                if (!castle.mapset_requested)
                {
                    std::wstring why;
                    if (!request_runtime_asset_load(pawn, mapset, &why, asset_request_id::castle_mapset))
                    {
                        throw std::runtime_error("kh02_entry_mapset_preload:" + to_utf8(why));
                    }
                    castle.mapset_requested = true;
                }
            }
            else
            {
                entry_assets.retain(mapset_asset);
            }
            // These sets are independent. Queue all of them on the first pass,
            // rather than waiting for Aqua, then mapset, then HUD, then stats.
            const bool hud_ready = hud.prepare(pawn);
            const bool stats_ready = enemy_stats.prepare(pawn);
            const bool ability_capacity_ready = ability_capacity.prepare(pawn, entry_assets);
            const bool aqua_level_ready = aqua_progression.prepare(pawn, entry_assets);
            if (!aqua_ready || !mapset_asset || !hud_ready || !stats_ready || !ability_capacity_ready ||
                !aqua_level_ready)
            {
                return false;
            }
            if (!castle.proof.contains("preload_ready_at_ms"))
            {
                castle.proof["preload_ready_at_ms"] = GetTickCount64();
                castle.proof["asset_completion_callbacks"] = entry_asset_callbacks;
            }
            castle.proof["prepared_shared_assets"] = entry_assets.paths();
            event_router.install();
            return true;
        }

        Kh02StoryNativeOverrides native_overrides;
        Kh02NativeAliases native_aliases;
        Kh02StoryMusic music{kh02_actor_world};

        void configure_music(UObject* world)
        {
            const auto name = world->GetPathName();
            const auto* room = mirror_room(name);
            int area = room ? room->area : 0;
            if (name == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
            {
                area = 2;
            }
            if (name == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
            {
                area = 3;
            }
            music.configure(world, area, entry.first_castle_intro);
        }

        void play_source_music(UObject* caller, UObject* sound, float seek, float fade)
        {
            music.play_source(caller, sound, seek, fade,
                              entry.first_castle_intro && opening_followup.state().observed() &&
                                  !opening_followup.state().handoff_ready(),
                              [this] { opening_followup.state().accept_music_handoff(); });
        }

        int counter_value{}, counter_maximum{5};
        bool counter_requested{};

        Kh02HelpContinuations help_callbacks;
        uint64_t magic_changes_blocked{}, help_windows_skipped{};
        int lamp_challenge_count{};
        std::string compat_error;
        std::string compat_warning;
        // Cosmetic forwarding failures remain diagnostic warnings. Invalid VM
        // operands/identities retire through the centralized recovery boundary.
        std::string compat_failure;
        FWeakObjectPtr compat_failure_world;
        FWeakObjectPtr recovery_world;
        std::string recovery_error;
        bool recovery_exit_dispatched{};
        uint64_t compat_calls{};
        uint64_t notices_skipped{};
        Json movie_calls = Json::array();
        Kh02NativeMovieProbe native_movie;
        Json last_mapset;
        uint64_t mapset_calls{};
        Kh02MovieBridge movie_bridge;
        Kh02CutsceneSession cutscenes{movie_bridge,
                                      {[this] { localization.prepare(); },
                                       [this](const wchar_t* text_namespace, const wchar_t* label, std::wstring* value)
                                       { return localization.lookup(text_namespace, label, value); },
                                       [](UObject* matinee) { return full_movie_for_scene(matinee); }}};
        Kh02Hud hud;
        Kh02EnemyStats enemy_stats;
        Kh02DarksideProjectileRepair darkside_projectiles;
        Kh02DarksidePillarRepair darkside_pillars;
        bool movie_fixture{}, native_movie_fixture{};

        static void have_all_signs(UObject* owner, FFrame& stack, void* result)
        {
            auto* self = compat_instance;
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (!self || !is_uobject_valid_fast(frame->Node) ||
                !frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/dw_03/umap/dw_03_challenge."))
            {
                if (self && self->native_overrides.original(Kh02StoryNativeOverrides::Slot::signs))
                {
                    self->native_overrides.original(Kh02StoryNativeOverrides::Slot::signs)(owner, stack, result);
                }
                return;
            }
            auto& script = frame->Node->GetScript();
            auto* end = script.GetData() + script.Num();
            if (!frame->Code || frame->Code < script.GetData() || end - frame->Code < 2 ||
                frame->Code[0] != kh02compat::opcode::self || frame->Code[1] != kh02compat::opcode::end_parameters ||
                !result)
            {
                self->compat_error = "The zodiac inventory query has an unexpected source operand.";
                return;
            }
            frame->Code += 2;
            *static_cast<uint8_t*>(result) =
                self->within_challenges[kh02_challenge_id::get_all_star_sign] == kh02_challenge_state::complete;
        }

        // Numeric IDs remain at the generated alias boundary; all interpretation
        // below it uses named adapters and one domain dispatch per invocation.
#include "kh3ap/features/kh02/compatibility/kh02_compatibility_dispatch.inl"

        template <int Id> static void compat_thunk(UObject* native_owner, FFrame& stack, void* return_value)
        {
            static_assert(kh02compat::valid_adapter(Id));
            auto* self = compat_instance;
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (!is_uobject_valid_fast(frame->Node))
            {
                if (self)
                {
                    self->compat_error = "kh02_compat_caller";
                    self->compat_failure = self->compat_error;
                    self->compat_failure_world = make_uobject_weak(kh02_current_story_world());
                }
                return;
            }
            auto& script = frame->Node->GetScript();
            auto* end = script.GetData() + script.Num();
            try
            {
                if (!self || !frame->Code || frame->Code < script.GetData() || frame->Code >= end ||
                    !frame->Node->GetPathName().starts_with(L"/Game/Z"))
                {
                    throw std::runtime_error("kh02_compat_caller");
                }
                if (self->recovery_world.Get() && self->recovery_world.Get() == kh02_current_story_world())
                {
                    throw std::runtime_error("kh02_compat_session_retired");
                }
                CompatibilityInvocation call(self, native_owner, frame, return_value,
                                             static_cast<kh02compat::Adapter>(Id));
                call.invoke();
            }
            catch (const std::exception& error)
            {
                if (self)
                {
                    self->compat_error = "adapter" + std::to_string(Id) + ":" + error.what();
                    self->compat_failure = self->compat_error;
                    self->compat_failure_world = make_uobject_weak(kh02_current_story_world());
                }
                if (script.Num() >= 3 && end[-3] == kh02compat::opcode::return_expression &&
                    end[-2] == kh02compat::opcode::nothing && end[-1] == kh02compat::opcode::end_script)
                {
                    frame->Code = end - 3;
                }
            }
        }

        void install_compat_aliases(kh02metadata::AliasPhase phase)
        {
#define KH02_COMPAT_ALIAS(row_phase, owner, name, prototype, handler)                                                  \
    if (phase == kh02metadata::AliasPhase::row_phase)                                                                  \
    {                                                                                                                  \
        native_aliases.add(owner, name, prototype, handler);                                                           \
    }
#include "kh02/compatibility_aliases.inc"
#undef KH02_COMPAT_ALIAS
        }

        void install_compat()
        {
            if (!native_aliases.empty())
            {
                return;
            }
            compat_instance = this;
            event_thread.store(GetCurrentThreadId(), std::memory_order_release);
            Kh02ReflectedCall callback_signature(L"/Script/Engine.Default__PrimitiveComponent",
                                                 L"SetTranslucentSortPriority", "kh02_asset_callback", 64);
            auto* callback_argument = callback_signature.param(L"NewTranslucentSortPriority");
            if (callback_signature.function->GetParmsSize() != sizeof(int32_t) || !callback_argument ||
                callback_argument->GetClass().GetName() != L"IntProperty" ||
                callback_argument->GetOffset_Internal() != 0)
            {
                throw std::runtime_error("kh02_asset_callback_signature");
            }
            native_aliases.add(L"/Script/Engine.Actor", L"KH02EntryAssetLoaded",
                               L"/Script/Engine.PrimitiveComponent:SetTranslucentSortPriority", entry_asset_loaded);
            native_aliases.add(L"/Script/Engine.LevelStreaming", L"KH02ArrivalLevelChanged",
                               L"/Script/Engine.Actor:OnRep_Owner", arrival_level_changed);
            set_native_chest_open_observer(&source_chest_opened);
            install_compat_aliases(kh02metadata::AliasPhase::initial);
            native_overrides.install_signs(have_all_signs);
            install_compat_aliases(kh02metadata::AliasPhase::after_signs);
            kh02_register_compatibility_enums(native_aliases);
            // The navigation-only source persistent script has no BeginPlay/Tick
            // dispatch. These imports retain harmless native descriptors for its
            // renamed overrides and unreachable old BGM nodes.
            install_compat_aliases(kh02metadata::AliasPhase::after_enums);
            encounter_proof["original_challenge35_requests"] = uint64_t(0);
            native_overrides.install_remaining(
                {encounter_play, source_information, target_activated}, [this](UObject* object, bool scene)
                { Kh02EncounterSceneBinding::bind_override(object, scene, encounter_services()); });
        }

        Kh02Localization localization;

        bool challenges_active() const
        {
            auto* pawn = gameplay.pawn.Get();
            return (entry.initialized || mirror.initialized) && is_uobject_valid_fast(pawn) &&
                   is_uobject_valid_fast(gameplay.world.Get()) && kh02_actor_world(pawn) == gameplay.world.Get();
        }

        void track_challenges(UObject* pawn)
        {
            // Castle Town uses an experimental DW host that the ordinary AP grant
            // gate excludes. Its entry routine already captured the possessed Sora
            // through GetPlayerPawn; keep using that weak, world-bound actor here.
            if (!is_uobject_valid_fast(pawn) || kh02_actor_world(pawn) != gameplay.world.Get())
            {
                pawn = gameplay.pawn.Get();
            }
            auto saved = reconciliation::view();
            const bool ready = challenges_active() && saved && saved->kh02_progress_ready &&
                               progress_session.matches(saved->seed, saved->slot, saved->kh02_progress_revision);
            if (ready && aqua_progression.prepare(pawn, entry_assets))
            {
                aqua_progression.bind(pawn, progress_session.revision(), source_flags);
                aqua_progression.refresh_sora(pawn);
            }
            else if (!ready)
            {
                aqua_progression.bind(nullptr, UINT64_MAX, source_flags);
            }
            challenge_tracker.bind(ready ? pawn : nullptr, ready ? gameplay.world.Get() : nullptr,
                                   progress_session.revision());
            if (ready && challenge_tracker.tick())
            {
                publish_mirror_progress();
            }
            if (ready && chest_observer.pending())
            {
                chest_observer.reconcile(gameplay.world.Get(), source_flags);
            }
        }

        Kh02MeshBufferLease mesh_buffers;

        Json native_before = Json::object();

        void bind_collapse()
        {
            kh02_prepare_collapse(gameplay.pawn.Get(), gameplay.world.Get(), event_aqua.actor(), cutscenes,
                                  movie_bridge, castle.proof);
        }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto diagnostics_common() -> Kh02StoryDiagnosticsCommon
        {
            return {source_flags,  hud,           enemy_stats, challenge_tracker, chest_observer, progress_session,
                    dressing_room, challenge_log, map_session, story_targets,     music,          cutscenes,
                    movie_bridge,  mesh_buffers};
        }

        Json flow_snapshot(UObject* pawn)
        {
            auto result = Kh02StoryDiagnostics::flow_snapshot(
                pawn, {diagnostics_common(), gameplay, castle, entry, localization, native_movie,
                       Json{{"aliases", native_aliases.size()},
                            {"calls", compat_calls},
                            {"error", compat_error},
                            {"warning", compat_warning},
                            {"counter_requested", counter_requested},
                            {"counter", counter_value},
                            {"maximum", counter_maximum},
                            {"magic_changes_blocked", magic_changes_blocked},
                            {"help_windows_skipped", help_windows_skipped},
                            {"help_continuations", help_callbacks.completed()},
                            {"notices_skipped", notices_skipped}},
                       native_before, encounter_proof, movie_calls, within_challenge_counts, within_challenges,
                       challenges_active(), postboss.state.observed, postboss.state.maximum_position,
                       postboss.state.requested, native_movie_fixture});
            result["postclear_treasure"] = postclear_treasure.snapshot();
            return result;
        }
#endif

        Kh02EventAqua event_aqua;
        FWeakObjectPtr entry_world;
        Kh02IntroPresentation intro_presentation;
        Kh02OpeningFollowup opening_followup;
        Kh02OpeningScene opening_scene{event_aqua, intro_presentation, music, cutscenes};
        Kh02StoryEventRouter event_router{mirror,
                                          story_targets,
                                          map_session,
                                          compat_error,
                                          {[this](UObject* object) { source_chest_opened(object); },
                                           [this](UObject* object) { host_begin_play(object); },
                                           [this](UObject* object)
                                           {
                                               darkside_projectiles.watch(object);
                                               darkside_pillars.watch(object);
                                           },
                                           [this](UObject* object)
                                           { chest_observer.mapset_finished(kh02_actor_world(object)); }}};

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        Json snapshot()
        {
            return Kh02StoryDiagnostics::opening_snapshot(
                {opening_scene, event_aqua, entry, intro_presentation, opening_followup});
        }
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        Json within_snapshot(UObject* pawn)
        {
            auto result = Kh02StoryDiagnostics::within_snapshot(pawn, {diagnostics_common(), mirror, information,
                                                                       island_materials, compat_error, compat_warning,
                                                                       mapset_calls, last_mapset, encounter_proof});
            result["darkside_projectiles"] = darkside_projectiles.snapshot();
            result["darkside_pillars"] = darkside_pillars.snapshot();
            result["postclear_treasure"] = postclear_treasure.snapshot();
            result["entrance_view_cleanup"] = encounter_proof.value("entrance_view_cleanup", Json::object());
            result["source_fade"] = intro_presentation.fade(pawn);
            return result;
        }
#endif

        auto mirror_services() -> Kh02MirrorGameplaySession::Services
        {
            return {entry,
                    gameplay,
                    event_aqua,
                    enemy_stats,
                    map_session,
                    postboss.matinee_binding(),
                    encounter_proof,
                    compat_error,
                    within_resume_entry,
                    mirror_travel.arrival_tag};
        }

        void tick_within(UObject* pawn, UObject* world)
        {
            mirror.tick(pawn, world, mirror_services());
        }

        bool finish_entry_levels()
        {
            return castle.finish({entry, gameplay, source_flags, event_aqua, map_session, encounter_proof});
        }

        void invalidate_progress_binding()
        {
            darkside_projectiles.reset();
            darkside_pillars.reset();
            map_session.cancel();
            map_session.invalidate_source_script();
            challenge_tracker.bind(nullptr, nullptr, UINT64_MAX);
            within_challenges.clear();
            within_challenge_counts.clear();
            challenge_events.clear();
            chest_observer.clear_world();
            source_flags.reset();
            progress_session.invalidate();
            within_resume_entry = false;
            castle.backtrack = false;
            gameplay.world = {};
            mirror.world = {};
            entry.initialized = false;
            mirror.initialized = false;
            castle.armed = false;
            castle.retry = false;
            entry.gameplay = false;
            entry.first_castle_intro = false;
            castle.preload_pawn = {};
            castle.backtrack_pending = false;
        }

        auto recovery_blocks_current_world() -> bool
        {
            if (recovery_error.empty())
            {
                return false;
            }
            auto* world = kh02_current_world();
            if (!is_uobject_valid_fast(world) || world == recovery_world.Get())
            {
                return true;
            }
            // A new native world owns a fresh session. An invalid old world is
            // never reused as authority for either recovery or normal gameplay.
            recovery_world = {};
            recovery_error.clear();
            recovery_exit_dispatched = false;
            return false;
        }

        auto reset_progress(UObject* pawn, UObject* w, bool inspect = false) -> std::string
        {
            if (!inspect)
            {
                return Kh02ProgressService::reset(pawn, [this] { invalidate_progress_binding(); });
            }
            if (!is_uobject_valid_fast(w) || (w->GetPathName() != L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01" &&
                                              w->GetPathName() != L"/Game/Maps/GardenOfAssemblage/umap/GoA_02.GoA_02"))
            {
                throw std::runtime_error("Return to the Garden before inspecting the 0.2 playtest");
            }
            const auto saved = reconciliation::view();
            if (!saved->ready || !saved->kh02_progress_ready)
            {
                throw std::runtime_error("The native save binding is not ready for a 0.2 reset");
            }
            return Json{{"progress", kh02_progress::to_json(saved->kh02_progress)},
                        {"revision", saved->kh02_progress_revision},
                        {"checks", saved->completed_checks},
                        {"challenges", within_challenges.size()},
                        {"counts", within_challenge_counts.size()},
                        {"events", challenge_events.size()}}
                .dump();
        }

        auto opening_context() -> Kh02OpeningScene::Context
        {
            return {gameplay.world.Get(), gameplay.script.Get(),    entry.gameplay,
                    entry.initialized,    entry.first_castle_intro, [this] { map_session.notify_readiness(); }};
        }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/kh02/diagnostics/kh02_story_diagnostics.inl"
#endif

        bool opening_chain_finished()
        {
            if (!entry.gameplay || !entry.first_castle_intro)
            {
                return true;
            }
            return opening_followup.finished(movie_bridge);
        }

        void tick(UObject* pawn, bool playable)
        {
            if (auto* initializer = castle.initializer.Get(); is_uobject_valid_fast(initializer))
            {
                initialize_castle_host(initializer);
                if (is_uobject_valid_fast(castle.initializer.Get()))
                {
                    return;
                }
            }
            auto* verified_world = is_uobject_valid_fast(pawn) ? kh02_actor_world(pawn) : nullptr;
            if (is_uobject_valid_fast(verified_world))
            {
                // Retire the old generation before any deferred source graph
                // can resume. The fallback owner below is not world authority.
                map_session.observe_world(verified_world);
            }
            track_challenges(pawn);
            if (challenges_active())
            {
                postclear_treasure.tick(pawn, source_flags.value(L"gameflow_REBELLIS_CLEAR") != 0);
            }
            // Arrival must finish streaming the authored PlayerStarts before the HUD
            // resolves the initial minimap section.
            hud.tick(pawn, !map_session.active() && entry.initialized && is_uobject_valid_fast(pawn) &&
                               kh02_actor_world(pawn) == gameplay.world.Get());
            // Resume the source latent link on a later service tick, after its
            // calling graph has returned. Neither KH3 magic state nor native help
            // UI is touched. Expired callbacks are discarded on map transitions.
            help_callbacks.tick(verified_world, map_session);
            native_movie.observe(gameplay.world.Get());
            timed_inputs.tick_action();
            auto* context = is_uobject_valid_fast(pawn) ? pawn : opening_scene.source_owner();
            auto* w = is_uobject_valid_fast(context) ? static_cast<AActor*>(context)->GetWorld() : nullptr;
            if (!is_uobject_valid_fast(w))
            {
                return;
            }
            story_targets.tick(w);
            tick_information(pawn, w);
            island_materials.prepare(context, w);
            if (entry.initialized && gameplay.world.Get() == w)
            {
                int area = 0;
                const auto name = w->GetPathName();
                if (name == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
                {
                    area = 2;
                }
                else if (name == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
                {
                    area = 3;
                }
                else if (const auto* r = mirror_room(name); r && r->area <= 10)
                {
                    area = r->area;
                }
                if (area && (area != 2 || !entry.first_castle_intro || (entry.done() && entry.failure().empty())))
                {
                    source_flags.record_visit(area);
                }
            }
            if (mirror_travel.dispatch(w, source_flags))
            {
                return;
            }
            if (mirror_room(w->GetPathName()))
            {
                tick_within(pawn, w);
                return;
            }
            if (w->GetPathName() == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
            {
                tick_within(pawn, w);
                return;
            }
            // KH3 Retry replaces the streamed scripts inside this same host World.
            // Rebind the new instances and run the source's actual retry entry;
            // retaining a valid host alone does not mean its encounter survived.
            if (entry.initialized && gameplay.world.Get() == w && !is_uobject_valid_fast(gameplay.script.Get()))
            {
                if (source_flags.value(L"gameflow_DW") == kh02_checkpoint::castle_tower_start &&
                    source_flags.value(L"gameflow_DW_GEAR_GETCOUNT") == kh02_gear_counter::all_collected)
                {
                    map_session.invalidate_source_script();
                    castle.retry = true;
                    entry.initialized = false;
                    entry.begin_boss_retry(std::chrono::steady_clock::now());
                    castle.gimmick_script = {};
                    postboss.clear_for_retry();
                    castle.proof["retry_scripts_replaced"] = true;
                }
            }
            if (entry.initialized && gameplay.world.Get() == w && is_uobject_valid_fast(pawn))
            {
                gameplay.pawn = make_uobject_weak(pawn);
                if (w->GetPathName() == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
                {
                    castle.shooting_star.tick(pawn, within_challenges[kh02_challenge_id::look_shooting_star] ==
                                                        kh02_challenge_state::complete);
                }
            }
            opening_scene.observe(w);
            cutscenes.tick_collapse(pawn, gameplay.world.Get(), event_aqua.actor());
            postboss.observe();
            if (postboss.dispatch(w, gameplay.world.Get(), encounter_proof))
            {
                return;
            }
            if (entry_world.Get() != w)
            {
                intro_presentation.set_hud_hidden(false);
                entry_world = make_uobject_weak(w);
                entry.reset_for_world(std::chrono::steady_clock::now());
                if (w->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
                {
                    return;
                }
                try
                {
                    const auto config_file =
                        kh02_read_runtime_config(L"kh02_story_config.json", kh02_entry_config_limit_bytes, true,
                                                 "story_config_image", "story_config_size");
                    if (!config_file)
                    {
                        return;
                    }
                    const auto& config = *config_file;
                    entry.gameplay = config.value("prepare_gameplay_on_entry", false);
                    entry.configure(entry.gameplay ||
                                        config.at("scene") == "dw02_dw009" && config.value("play_on_entry", false),
                                    entry.gameplay);
                }
                catch (const std::exception& e)
                {
                    entry.fail(e.what());
                }
            }
            if (!entry.enabled || entry.done())
            {
                return;
            }
            try
            {
                const Kh02EntrySession::Environment environment{
                    entry.gameplay ? runtime_sora_control_ready(context) : playable,
                    map_session.active() && map_session.world() == w, map_session.phase() == Kh02ArrivalPhase::Opening,
                    map_session.active() && map_session.phase() != Kh02ArrivalPhase::Opening};
                entry.tick(environment,
                           {[this] { finish_entry_levels(); },
                            [this, context] { return opening_scene.prepare(context, opening_context()); },
                            [this, context] { opening_scene.start(context, opening_context()); },
                            [this, context] { return opening_scene.unload(context, opening_context()); },
                            [this] { return opening_scene.observation(); }, [this] { return opening_chain_finished(); },
                            [this] { music.release_opening_event(); }, [] { return std::chrono::steady_clock::now(); },
                            [this] { return map_session.phase() == Kh02ArrivalPhase::Opening; }, [this]
                            { return map_session.active() && map_session.phase() != Kh02ArrivalPhase::Opening; }},
                           std::chrono::steady_clock::now());
            }
            catch (const std::exception& e)
            {
                entry.fail(e.what());
                if (opening_scene.was_dispatched() && is_uobject_valid_fast(opening_scene.source_script()))
                {
                    try
                    {
                        opening_scene.skip(context, opening_context());
                    }
                    catch (...)
                    {
                    }
                }
                intro_presentation.set_hud_hidden(false);
            }
        }
    };

    Kh02Story::Kh02Story(std::function<Kh02BattleLevelSettings()> settings)
        : impl(std::make_unique<Impl>(std::move(settings)))
    {
    }

    void Kh02Story::tick_enemy_level(UObject* pawn)
    {
        impl->enemy_stats.tick_hero(kh02_story_player(pawn));
    }

    Kh02Story::~Kh02Story() = default;

    auto Kh02Story::active_session() const -> bool
    {
        return is_uobject_valid_fast(kh02_current_story_world());
    }

    auto Kh02Story::failure() const -> std::string
    {
        auto* world = kh02_current_story_world();
        if (!is_uobject_valid_fast(world))
        {
            return {};
        }
        if (impl->recovery_world.Get() == world && !impl->recovery_error.empty())
        {
            return impl->recovery_error;
        }
        if (impl->entry_world.Get() == world && !impl->entry.failure().empty())
        {
            return impl->entry.failure();
        }
        if (impl->mirror.world.Get() == world && !impl->mirror.error.empty())
        {
            return impl->mirror.error;
        }
        if (impl->compat_failure_world.Get() == world && !impl->compat_failure.empty())
        {
            return impl->compat_failure;
        }
        if (impl->map_session.world() == world && !impl->map_session.failure().empty())
        {
            return impl->map_session.failure();
        }
        if (impl->gameplay.world.Get() == world)
        {
            if (!impl->movie_bridge.error().empty())
            {
                return impl->movie_bridge.error();
            }
            if (!impl->cutscenes.failure().empty())
            {
                return impl->cutscenes.failure();
            }
        }
        return {};
    }

    void Kh02Story::retire_failed_session(std::string_view reason)
    {
        auto* world = kh02_current_story_world();
        if (!is_uobject_valid_fast(world) || (impl->recovery_world.Get() == world && !impl->recovery_error.empty()))
        {
            return;
        }
        impl->recovery_world = make_uobject_weak(world);
        impl->recovery_error = reason.empty() ? "kh02_session_failed" : std::string(reason);
        impl->recovery_exit_dispatched = false;
        impl->retirement().run(Kh02RetirementReason::Failure);
    }

    auto Kh02Story::exit_failed_session(UObject* pawn) -> bool
    {
        auto* hero = kh02_story_player(pawn);
        auto* world = kh02_current_story_world();
        if (!is_uobject_valid_fast(world) || !is_uobject_valid_fast(hero) || kh02_actor_world(hero) != world ||
            impl->recovery_world.Get() != world || impl->recovery_error.empty() || impl->recovery_exit_dispatched)
        {
            return false;
        }
        auto* controller = try_read_reflected_object_property_value(hero, L"Controller");
        if (!is_uobject_valid_fast(controller) ||
            try_read_reflected_object_property_value(controller, L"Pawn") != hero || !Impl::loading_closed())
        {
            return false;
        }
        ReflectedCall paused(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
        paused.object(L"WorldContextObject", hero);
        paused.run();
        if (paused.boolean_result())
        {
            return false;
        }
        ReflectedCall travel(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                             L"TresMapJump");
        travel.name(L"MapName", L"/Game/Maps/GardenOfAssemblage/umap/GoA_01");
        travel.name(L"TargetTag", L"NormalSpawn");
        travel.integer(L"InFadeKind", 0);
        travel.run();
        impl->recovery_exit_dispatched = true;
        return true;
    }

    auto Kh02Story::recovery_garden_ready(UObject* pawn) const -> bool
    {
        auto* hero = kh02_story_player(pawn);
        auto* world = kh02_current_world();
        if (!is_uobject_valid_fast(world) || !is_uobject_valid_fast(hero) || kh02_actor_world(hero) != world ||
            world->GetPathName() != L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01" || !Impl::loading_closed() ||
            !runtime_sora_control_ready(hero))
        {
            return false;
        }
        ReflectedCall paused(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
        paused.object(L"WorldContextObject", hero);
        paused.run();
        return !paused.boolean_result();
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02Story::movie_snapshot() const -> std::string
    {
        return impl->movie_bridge.snapshot().dump();
    }
#endif

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02Story::command(UObject* pawn, std::wstring_view action) -> std::optional<std::string>
    {
        return impl->diagnostic_command(kh02_story_player(pawn), action);
    }
#endif

    auto Kh02Story::current_hero() -> UObject*
    {
        return kh02_story_player(nullptr);
    }

    void Kh02Story::refresh_sora_experience(UObject* pawn)
    {
        if (auto* self = Impl::compat_instance)
        {
            self->aqua_progression.refresh_sora(pawn);
        }
    }

    auto Kh02Story::aqua_experience(UObject* pawn) -> std::optional<int>
    {
        auto* self = Impl::compat_instance;
        return self && map_overlay_hero() == pawn ? self->aqua_progression.experience(pawn) : std::nullopt;
    }

    auto Kh02Story::award_shared_experience(void* save_owner, int amount, int lane) noexcept -> bool
    {
        auto* self = Impl::compat_instance;
        if (!self || lane != 0 || amount <= 0 ||
            self->event_thread.load(std::memory_order_acquire) != GetCurrentThreadId())
        {
            return false;
        }
        auto* pawn = map_overlay_hero();
        if (!is_uobject_valid_fast(pawn) || (pawn->GetClassPrivate()->GetName() != L"p_ex002_Pawn_C" &&
                                              pawn->GetClassPrivate()->GetName() != L"p_ex001_Pawn_C"))
        {
            return false;
        }
        const bool aqua = pawn->GetClassPrivate()->GetName() == L"p_ex002_Pawn_C";
        try
        {
            const auto saved = reconciliation::view();
            if (!saved || !saved->kh02_progress_ready ||
                !self->progress_session.matches(saved->seed, saved->slot, saved->kh02_progress_revision))
            {
                throw std::runtime_error("kh02_aqua_experience_save_binding");
            }
            self->aqua_progression.bind(pawn, self->progress_session.revision(), self->source_flags);
            const auto levels = self->enemy_stats.experience_levels(pawn, self->aqua_progression.sora_level(save_owner));
            self->aqua_progression.award(pawn, save_owner, amount, levels.first, levels.second, self->source_flags);
        }
        catch (const std::exception& error)
        {
            const std::string message = error.what();
            RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] KH0.2 shared EXP award failed: {}\n"),
                                                  std::wstring(message.begin(), message.end()));
        }
        // Sora retains his native award and level-up presentation.
        return aqua;
    }

    auto Kh02Story::persistent_reward_context() -> UObject*
    {
        auto* self = Impl::compat_instance;
        if (!self || !kh02_current_story_world())
        {
            return nullptr;
        }
        auto* pawn = kh02_story_player(nullptr);
        if (!is_uobject_valid_fast(pawn) || pawn->GetClassPrivate()->GetName() != L"p_ex002_Pawn_C" ||
            !map_overlay_ready(pawn) || !self->loading_closed() || !runtime_sora_control_ready(pawn) ||
            !self->cutscenes.failure().empty() || !kh02_companion_health().ready)
        {
            return nullptr;
        }
        try
        {
            ReflectedCall paused(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"),
                                 L"IsGamePaused");
            paused.object(L"WorldContextObject", pawn);
            paused.run();
            return paused.boolean_result() ? nullptr : pawn;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    auto Kh02Story::map_overlay_hero() -> UObject*
    {
        auto* self = Impl::compat_instance;
        auto* pawn = self ? self->gameplay.pawn.Get() : nullptr;
        if (!native_save_ready(pawn))
        {
            return nullptr;
        }
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        auto* instance = try_read_reflected_object_property_value(kh02_actor_world(pawn), L"OwningGameInstance");
        auto* manager = try_read_reflected_object_property_value(instance, L"m_UIManager");
        if (!is_uobject_valid_fast(controller) ||
            try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
            try_read_reflected_object_property_value(manager, L"m_pPlayerController") != controller)
        {
            return nullptr;
        }
        return pawn;
    }

    auto Kh02Story::map_overlay_ready(UObject* verified_hero) -> bool
    {
        // The overlay resolves and checks ownership immediately before calling
        // this predicate. Reuse that local result rather than reflecting it again.
        auto* pawn = verified_hero ? verified_hero : map_overlay_hero();
        auto* self = Impl::compat_instance;
        if (!self || !is_uobject_valid_fast(pawn) || self->gameplay.pawn.Get() != pawn || !native_save_ready(pawn) ||
            self->map_session.active() || self->challenge_log.active() || self->cutscenes.event_aqua_playing() ||
            flag(pawn, L"bHidden"))
        {
            return false;
        }
        ReflectedCall state(pawn, L"GetStateID");
        state.run();
        const auto state_id = read_function_int_param(state.function, state.parameters.data(), L"ReturnValue");
        return state_id != 67 && state_id != 59;
    }

    auto Kh02Story::gameflow_command(UObject* pawn, std::wstring_view request_id, std::wstring_view command,
                                     std::wstring_view flag_name, std::optional<int> value) -> std::string
    {
        return Kh02ProgressService::command(pawn, request_id, command, flag_name, value,
                                            []
                                            {
                                                if (Impl::compat_instance)
                                                {
                                                    Impl::compat_instance->invalidate_progress_binding();
                                                }
                                            });
    }

    void Kh02Story::tick(UObject* pawn, bool playable)
    {
        if (impl->recovery_blocks_current_world())
        {
            return;
        }
        auto* hero = kh02_story_player(pawn);
        impl->tick(hero, playable);
        impl->dressing_room.tick(hero, impl->challenges_active());
    }

    auto Kh02Story::preparation_pending() const -> bool
    {
        return impl->map_session.active();
    }

    auto Kh02Story::entry_assets_notified() const -> bool
    {
        return impl->entry_asset_completion;
    }

    void Kh02Story::tick_preparation(UObject* pawn)
    {
        // Arrival covers may be armed in GoA before travel. This does not run
        // challenges, HUD polling, movies, or the normal story update.
        auto* hero = kh02_story_player(pawn);
        if (is_uobject_valid_fast(hero))
        {
            impl->map_session.observe_world(kh02_actor_world(hero));
        }
        impl->map_session.tick(hero);
    }

    void Kh02Story::leave_world(UObject* destination)
    {
        if (!is_uobject_valid_fast(destination) || kh02_world_kind(destination) == Kh02WorldKind::story)
        {
            return;
        }
        impl->enemy_stats.release_hero(destination);
        // Invalidate callbacks before releasing independent resources. Keep
        // progress and native aliases: live engine imports still reference them.
        const auto cleanup_error = impl->retirement().run(Kh02RetirementReason::WorldDeparture, destination);
        impl->recovery_blocks_current_world();
        if (!cleanup_error.empty())
        {
            throw std::runtime_error(cleanup_error);
        }
    }

    auto Kh02Story::needs_frame() const -> bool
    {
        if (!impl->recovery_error.empty() &&
            (!is_uobject_valid_fast(kh02_current_world()) || impl->recovery_world.Get() == kh02_current_world()))
        {
            return false;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (kh02_mirror_transition_diagnostics::pending())
        {
            return true;
        }
#endif
        return impl->map_session.active() || impl->timed_inputs.walk_active() || impl->cutscenes.needs_frame() ||
               impl->movie_bridge.needs_tick() || impl->challenge_log.needs_tick() ||
               impl->dressing_room.needs_tick() || impl->darkside_projectiles.needs_frame() ||
               impl->darkside_pillars.needs_frame();
    }

    void Kh02Story::tick_frame(UObject* pawn)
    {
        if (!needs_frame())
        {
            return;
        }
        auto* hero = kh02_story_player(pawn);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        kh02_mirror_transition_diagnostics::tick();
#endif
        if (is_uobject_valid_fast(hero))
        {
            // Retire old presentation before either movie callbacks or an EOF
            // hold can resume its graph between the slower story polls.
            impl->map_session.observe_world(kh02_actor_world(hero));
        }
        impl->dressing_room.tick(hero, impl->challenges_active());
        impl->map_session.tick(hero);
        if (!failure().empty())
        {
            return;
        }
        impl->darkside_projectiles.tick(hero, is_uobject_valid_fast(hero) ? kh02_actor_world(hero) : nullptr);
        impl->darkside_pillars.tick(is_uobject_valid_fast(hero) ? kh02_actor_world(hero) : nullptr);
        if (impl->timed_inputs.walk_active())
        {
            const bool castle_scene_started =
                impl->timed_inputs.walk_stops_at_castle_scene() &&
                impl->source_flags.value(L"gameflow_DW") >= kh02_checkpoint::gear_scene_dw012;
            impl->timed_inputs.tick_walk(hero, castle_scene_started);
        }
        impl->movie_bridge.tick(hero);
        if (!failure().empty())
        {
            return;
        }
        const bool scene_was_playing = impl->cutscenes.event_aqua_playing();
        impl->cutscenes.tick(hero, impl->gameplay.world.Get(), impl->event_aqua.actor());
        if (scene_was_playing && !impl->cutscenes.event_aqua_playing())
        {
            try_kh02_music_execution([&] { impl->music.release_scene_field(); },
                                     [&](const char* warning) { impl->compat_warning = warning; });
        }
        if (impl->challenge_log.needs_tick())
        {
            // The loading transition can retire latent requests and transient
            // movies. Prepare the menu only after this host has been revealed.
            impl->challenge_log.tick(hero, impl->challenges_active() && !impl->map_session.active());
        }
    }

    auto Kh02Story::challenges_available() -> bool
    {
        return Impl::compat_instance && Impl::compat_instance->challenges_active();
    }

    auto Kh02Story::native_save_ready(UObject* pawn) -> bool
    {
        auto* self = Impl::compat_instance;
        auto* w = kh02_actor_world(pawn);
        if (!self || !is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(w) || self->gameplay.world.Get() != w ||
            !self->entry.initialized)
        {
            return false;
        }
        const auto name = w->GetPathName();
        if (name == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
            name == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
        {
            return true;
        }
        const auto* room = mirror_room(name);
        return room && room->area < 50 && self->mirror.initialized && self->mirror.error.empty();
    }

    auto Kh02Story::challenges_open() -> bool
    {
        return Impl::compat_instance && Impl::compat_instance->challenge_log.active();
    }

    auto Kh02Story::route_challenges(int controller, std::wstring_view key, int event, bool gamepad) -> bool
    {
        return Impl::compat_instance && Impl::compat_instance->challenge_log.route(controller, key, event, gamepad);
    }

    auto Kh02Story::open_challenges(UObject* camp) -> bool
    {
        auto* self = Impl::compat_instance;
        if (!self || !self->challenges_active())
        {
            return false;
        }
        auto rows = kh02_challenge_rows(self->within_challenges, self->within_challenge_counts);
        return self->challenge_log.open(camp, std::move(rows));
    }

    void Kh02Story::release_dressing_room(bool restore_materials)
    {
        if (Impl::compat_instance)
        {
            Impl::compat_instance->dressing_room.release(restore_materials);
        }
    }

    auto Kh02Story::open_dressing_room(UObject* camp) -> bool
    {
        auto* self = Impl::compat_instance;
        if (!self || !self->challenges_active())
        {
            return false;
        }
        return self->challenge_log.open_costumes(
            camp, [self](int category) { return self->dressing_room.list(category); }, [self](int category, int index)
            { self->dressing_room.choose(category, index); }, [self]() { return self->dressing_room.selection(); },
            [self](const std::string& selection) { self->dressing_room.set_selection(selection); },
            [self](bool visible, float angle) { self->dressing_room.preview(visible, angle); });
    }

    auto Kh02Story::open_area_select(UObject* menu, std::function<void(int)> selected) -> bool
    {
        auto* self = Impl::compat_instance;
        if (!self || !self->challenges_active() || !selected)
        {
            return false;
        }
        return kh02_open_area_selection(
            self->challenge_log, menu, [self](const std::wstring& name) { return self->source_flags.value(name); },
            std::move(selected));
    }

    auto Kh02Story::travel_to_visited_area(int area) -> bool
    {
        auto* self = Impl::compat_instance;
        auto* pawn = kh02_story_player(nullptr);
        if (!self || !is_uobject_valid_fast(pawn) || kh02_actor_world(pawn) != self->gameplay.world.Get())
        {
            return false;
        }
        const auto* destination = kh02_visited_area_destination(area, [self](const std::wstring& name)
                                                                { return self->source_flags.value(name); });
        if (!destination)
        {
            return false;
        }
        if (area == 2 && !self->prepare_entry(pawn))
        {
            return false;
        }
        self->castle.backtrack_pending = area == 2;
        const auto* arrival_tag =
            kh02_area_resume_tag(area, self->source_flags.value(L"gameflow_DW"), destination->tag);
        self->mirror_travel.arrival_tag = arrival_tag;
        self->mirror_travel.arrival_package = destination->package;
        const std::wstring package = destination->package;
        self->map_session.arm(pawn, package + L"." + package.substr(package.find_last_of(L'/') + 1));
        ReflectedCall travel(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                             L"TresMapJump");
        travel.name(L"MapName", destination->package);
        travel.name(L"TargetTag", arrival_tag);
        travel.integer(L"InFadeKind", 1);
        travel.run();

        return true;
    }

    auto Kh02Story::prepare_entry(UObject* pawn) -> bool
    {
        return impl->prepare_entry(pawn);
    }

} // namespace kh3ap
