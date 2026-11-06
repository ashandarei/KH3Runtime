#include "kh02_opening_scene.h"
#include "kh02_event_aqua.h"
#include "kh02_intro_presentation.h"
#include "kh02_cutscene_session.h"
#include "kh02_cutscene_reflection.h"
#include "kh02_matinee_binding.h"
#include "kh3ap/features/kh02/story/kh02_story_music.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02cutscene_detail;

    namespace
    {
        constexpr auto opening_scene_path = L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw009";
        constexpr int class_actor_scan_limit = 256;
        constexpr float scene_skip_music_fade_seconds = 1.0f;
    } // namespace

    Kh02OpeningScene::Kh02OpeningScene(Kh02EventAqua& aqua, Kh02IntroPresentation& presentation,
                                       Kh02StoryMusic& story_music, Kh02CutsceneSession& scene_session)
        : event_aqua(aqua), intro_presentation(presentation), music(story_music), cutscenes(scene_session)
    {
    }

    auto Kh02OpeningScene::bind_context(UObject*& pawn, const Context& context) -> UObject*
    {
        if (!is_uobject_valid_fast(pawn))
        {
            pawn = owner.Get();
        }
        auto* scene_world = is_uobject_valid_fast(pawn) ? static_cast<AActor*>(pawn)->GetWorld() : nullptr;
        if (!is_uobject_valid_fast(scene_world) ||
            scene_world->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
        {
            throw std::runtime_error("kh02_story_requires_castle_host");
        }
        if (world.Get() != scene_world)
        {
            owner = make_uobject_weak(pawn);
            world = make_uobject_weak(scene_world);
            stream = {};
            if (context.gameplay_world != scene_world || !is_uobject_valid_fast(event_aqua.actor()) ||
                kh02_actor_world(event_aqua.actor()) != scene_world)
            {
                event_aqua.forget_actor();
            }
            script = {};
            matinee = {};
            event_aqua.reset_request();
            dispatched = false;
        }
        return scene_world;
    }

    void Kh02OpeningScene::require_loaded_scene() const
    {
        if (!is_uobject_valid_fast(script.Get()) || !is_uobject_valid_fast(matinee.Get()))
        {
            throw std::runtime_error("kh02_story_load_first");
        }
    }

    auto Kh02OpeningScene::prepare(UObject* pawn, const Context& context) -> bool
    {
        bind_context(pawn, context);

        if (!event_aqua.prepare(pawn))
        {
            return false;
        }
        if (!is_uobject_valid_fast(stream.Get()))
        {
            ReflectedCall load(kh02_require_loaded_object(L"/Script/Engine.Default__LevelStreamingKismet"),
                               L"LoadLevelInstance");
            const std::wstring level = opening_scene_path;
            load.object(L"WorldContextObject", pawn);
            load.string(L"LevelName", level);
            load.vector(L"Location", 0, 0, 0);
            load.vector(L"Rotation", 0, 0, 0);
            load.run();
            auto* level_stream = load.result();
            if (!load.boolean_result(L"bOutSuccess") || !is_uobject_valid_fast(level_stream))
            {
                throw std::runtime_error("kh02_story_stream_failed");
            }
            stream = make_uobject_weak(level_stream);
            return false;
        }
        auto* level_stream = stream.Get();
        ReflectedCall visible(level_stream, L"IsLevelVisible");
        visible.run();
        if (!visible.boolean_result())
        {
            return false;
        }
        auto* level = try_read_reflected_object_property_value(level_stream, L"LoadedLevel");
        auto* source_script = try_read_reflected_object_property_value(level, L"LevelScriptActor");
        if (!is_uobject_valid_fast(source_script))
        {
            throw std::runtime_error("kh02_story_level_script");
        }
        script = make_uobject_weak(source_script);
        if (!is_uobject_valid_fast(matinee.Get()))
        {
            auto* matinee_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.MatineeActor");
            for (auto* actor : find_live_actors_of_class(pawn, matinee_class, class_actor_scan_limit))
            {
                if (!is_uobject_valid_fast(actor) || actor->GetOuterPrivate() != level || actor->GetName() != L"dw009")
                {
                    continue;
                }
                if (is_uobject_valid_fast(matinee.Get()))
                {
                    throw std::runtime_error("kh02_story_ambiguous_matinee");
                }
                matinee = make_uobject_weak(actor);
            }
            if (!is_uobject_valid_fast(matinee.Get()))
            {
                throw std::runtime_error("kh02_story_matinee_missing");
            }
            // Bind the original serialized, single-actor Aqua group before
            // InitInterp. No array allocation or guessed native actor ID.
            auto** slot = kh02_matinee_actor_slot(matinee.Get());
            if (*slot && *slot != event_aqua.actor())
            {
                throw std::runtime_error("kh02_story_aqua_group_occupied");
            }
            kh02_bind_event_aqua(matinee.Get(), event_aqua.actor(), L"TresInterpGroup_21");
            cutscenes.track_event_aqua(matinee.Get(), event_aqua.actor());
            // This level is created after arrival subscribes to the host's
            // streams. Reuse preparation's completion observation to wake the
            // arrival gate once, after the dynamic scene is visible and bound.
            if (context.notify_ready)
            {
                context.notify_ready();
            }
        }
        return true;
    }

    auto Kh02OpeningScene::unload(UObject* pawn, const Context& context) -> bool
    {
        auto* scene_world = bind_context(pawn, context);

        if (auto* scene_actor = matinee.Get(); is_uobject_valid_fast(scene_actor) && flag(scene_actor, L"bIsPlaying"))
        {
            throw std::runtime_error("kh02_story_skip_before_unload");
        }
        if (dispatched && observed_playing && !is_uobject_valid_fast(script.Get()))
        {
            if (!intro_presentation.finish(pawn, dispatched && observed_playing && !is_uobject_valid_fast(script.Get()),
                                           maximum_position))
            {
                return false;
            }
        }
        if (auto* level_stream = stream.Get(); is_uobject_valid_fast(level_stream))
        {
            set_flag(level_stream, L"bShouldBeVisible", false);
            set_flag(level_stream, L"bShouldBeLoaded", false);
        }
        if (auto* actor = event_aqua.actor(); is_uobject_valid_fast(actor))
        {
            if (context.gameplay && context.initialized && context.gameplay_world == scene_world)
            {
                // Later Castle Town scenes reuse this host-owned actor.
                ReflectedCall hide(actor, L"SetActorHiddenInGame");
                hide.boolean(L"bNewHidden", true);
                hide.run();
            }
            else
            {
                ReflectedCall destroy(actor, L"K2_DestroyActor");
                destroy.run();
                event_aqua.forget_actor();
            }
        }
        script = {};
        matinee = {};
        stream = {};
        dispatched = false;
        intro_presentation.set_hud_hidden(false);
        return true;
    }

    void Kh02OpeningScene::start(UObject* pawn, const Context& context)
    {
        bind_context(pawn, context);
        require_loaded_scene();

        if (dispatched)
        {
            throw std::runtime_error("kh02_story_already_dispatched");
        }
        observed_playing = false;
        maximum_position = 0;
        intro_presentation.reset_cleanup();
        intro_presentation.set_hud_hidden(true);
        // The host has already entered gameplay. Restore the original
        // cinematic owner'level_stream player/cockpit/input gate before DW009.
        // Retirement cleanup balances this mode before gameplay resumes.
        ReflectedCall cinematic(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                L"TresStartCinematicMode");
        cinematic.object(L"WorldContextObject", pawn);
        for (const auto* flag : {L"bHidePlayer", L"bHideCockpit", L"bDisableMove", L"bPlayerStateClear",
                                 L"bDisableCameraMove", L"bPauseAI", L"bDisableIKinema"})
        {
            cinematic.boolean(flag, true);
        }
        cinematic.run();
        if (context.gameplay && context.first_castle_intro)
        {
            // Match the original DW009 dispatcher: leave field music
            // stopped, then let real source completions select the cues.
            ReflectedCall sound_mode(kh02_require_loaded_object(L"/Script/TresGame.Default__TresSoundStatics"),
                                     L"TresSoundMode_End");
            sound_mode.integer(L"mode", 0);
            sound_mode.run();
            music.stop_source(context.gameplay_script, nullptr, scene_skip_music_fade_seconds);
        }
        ReflectedCall show(event_aqua.actor(), L"SetActorHiddenInGame");
        show.boolean(L"bNewHidden", false);
        show.run();
        ReflectedCall start(script.Get(), L"dw_02_gameflow_Evt_cutscene_dw02_dw009");
        if (start.function->GetParmsSize())
        {
            throw std::runtime_error("kh02_story_start_signature");
        }
        start.run();
        dispatched = true;
    }

    void Kh02OpeningScene::skip(UObject* pawn, const Context& context)
    {
        bind_context(pawn, context);
        require_loaded_scene();

        ReflectedCall skip(script.Get(), L"OnEventSkip");
        skip.run();
    }

    void Kh02OpeningScene::observe(UObject* current_world)
    {
        if (!dispatched || world.Get() != current_world)
        {
            return;
        }
        if (auto* scene_actor = matinee.Get(); is_uobject_valid_fast(scene_actor))
        {
            observed_playing |= flag(scene_actor, L"bIsPlaying");
            maximum_position = std::max(maximum_position, real(scene_actor, L"InterpPosition"));
        }
    }

    auto Kh02OpeningScene::observation() const -> Kh02EntrySession::SceneObservation
    {
        auto* scene_actor = matinee.Get();
        const bool valid = is_uobject_valid_fast(scene_actor);
        return {valid, valid && flag(scene_actor, L"bIsPlaying"), is_uobject_valid_fast(script.Get()),
                valid ? real(scene_actor, L"InterpPosition") : 0.f};
    }
} // namespace kh3ap
