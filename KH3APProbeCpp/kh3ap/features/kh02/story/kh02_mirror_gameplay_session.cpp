#include "kh02_mirror_gameplay_session.h"
#include "kh02_source_level_readiness.h"
#include "kh02_mirror_arrival.h"
#include "kh02_entry_session.h"
#include "kh02_map_session.h"
#include "kh3ap/features/kh02/combat/kh02_enemy_stats.h"
#include "kh3ap/features/kh02/cutscenes/kh02_event_aqua.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_reflection.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/core/kh02_world_identity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/kh02/cutscenes/kh02_intro_presentation.h"

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02cutscene_detail;

    namespace
    {
        auto mirror_room(std::wstring_view world) -> const kh02mirror_metadata::Room*
        {
            return kh02_world_identity::mirror_room(world);
        }
    } // namespace

    void Kh02MirrorGameplaySession::begin_hub(UObject* current_world, Services services)
    {
        world = make_uobject_weak(current_world);
        script = {};
        entrance_finish_script = {};
        entrance_reveal_requested = false;
        horizon.reset();
        entrance_mirror.clear();
        services.encounter_proof.erase("entrance_mirror");
        services.encounter_proof.erase("entrance_view_cleanup");
        initialized = false;
        intro_started = false;
        error.clear();
        services.gameplay.world = make_uobject_weak(current_world);
        services.gameplay.pawn = {};
        services.event_aqua.reset();
    }

    void Kh02MirrorGameplaySession::begin_room(UObject* current_world, UObject* source_initializer, Services services)
    {
        world = make_uobject_weak(current_world);
        initialized = false;
        services.entry.initialized = false;
        initializer = make_uobject_weak(source_initializer);
        error.clear();
        forest_handoff.clear();
        forest_story_proof = nlohmann::json::object();
        forest_visibility.clear_world();
        services.gameplay.world = make_uobject_weak(current_world);
        services.gameplay.pawn = {};
        services.event_aqua.reset();
        finish_initialization(source_initializer, services);
    }

    void Kh02MirrorGameplaySession::finish_initialization(UObject* context, Services services)
    {
        // These loaders explicitly return false while their async requests
        // are pending. Keep the arrival cover and resume this initializer;
        // a requested class/table is not a permanent initialization error.
        if (!services.event_aqua.prepare(context) || !services.enemy_stats.prepare(context))
        {
            return;
        }
        initialized = services.entry.initialized = true;
        initializer = {};
    }

    void Kh02MirrorGameplaySession::source_entrance_finished(UObject* source_script)
    {
        if (world.Get() == kh02_actor_world(source_script) && script.Get() == source_script)
        {
            entrance_finish_script = make_uobject_weak(source_script);
            entrance_reveal_requested = false;
        }
    }

    void Kh02MirrorGameplaySession::finish_entrance_view(UObject* pawn, bool scene_playing, Services services)
    {
        auto* source_script = entrance_finish_script.Get();
        if (!is_uobject_valid_fast(source_script))
        {
            return;
        }
        if (source_script != script.Get() || kh02_actor_world(source_script) != world.Get())
        {
            entrance_finish_script = {};
            return;
        }
        // The real natural/Skip source event wakes this work. Its latent
        // continuation must also finish and return gameplay before touching
        // the final Matinee fade; never reveal a live cinematic or map jump.
        if (scene_playing || services.map_session.active() || !runtime_sora_control_ready(pawn))
        {
            return;
        }
        auto& proof = services.encounter_proof["entrance_view_cleanup"];
        Kh02IntroPresentation view;
        if (!entrance_reveal_requested)
        {
            proof = {{"before", view.fade(pawn)}, {"source_finished", true}, {"verified", false}};
            ReflectedCall reveal(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                 L"BPTresCancelFade");
            reveal.object(L"WorldContextObject", pawn);
            reveal.integer(L"InLayer", 0);
            reveal.number(L"InFadeTime", .3f);
            reveal.boolean(L"InApplyGameSpeed", false);
            reveal.run();
            entrance_reveal_started = std::chrono::steady_clock::now();
            entrance_reveal_requested = true;
        }
        proof["after"] = view.fade(pawn);
        const auto& fade = proof["after"];
        if (fade["amount"].get<float>() <= .001f && !fade["changing"].get<bool>())
        {
            proof["verified"] = true;
            entrance_finish_script = {};
        }
        else if (std::chrono::steady_clock::now() - entrance_reveal_started > std::chrono::seconds(5))
        {
            entrance_finish_script = {};
            services.compat_error = "kh02_within_entrance_fade_cleanup_timeout";
        }
    }

    void Kh02MirrorGameplaySession::tick(UObject* pawn, UObject* current_world, Services services)
    {
        if (world.Get() != current_world || !is_uobject_valid_fast(pawn) || !error.empty())
        {
            return;
        }
        services.gameplay.world = make_uobject_weak(current_world);
        services.gameplay.pawn = make_uobject_weak(pawn);
        if (!initialized && mirror_room(current_world->GetPathName()))
        {
            try
            {
                auto* source_initializer = initializer.Get();
                if (!is_uobject_valid_fast(source_initializer) || kh02_actor_world(source_initializer) != current_world)
                {
                    throw std::runtime_error("The 0.2 mirror initializer no longer belongs to this world");
                }
                finish_initialization(source_initializer, services);
            }
            catch (const std::exception& error)
            {
                this->error = error.what();
            }
            return;
        }
        if (initialized)
        {
            if (auto error = forest_visibility.tick(pawn, current_world, !services.map_session.active()))
            {
                this->error = std::move(*error);
            }
            if (auto error = forest_handoff.tick(pawn, current_world, forest_story_proof))
            {
                services.compat_error = std::move(*error);
            }
            // The adapted event pawn belongs to the host, so unloading the
            // source cutscene cannot retire it. Hide it after natural finish
            // or native Skip, based on the real Matinee's playback state.
            auto* scene = services.postboss_matinee.Get();
            auto* event_pawn = services.event_aqua.actor();
            const bool playing = is_uobject_valid_fast(scene) && flag(scene, L"bIsPlaying");
            finish_entrance_view(pawn, playing, services);
            if (auto error =
                    entrance_mirror.tick(scene, services.gameplay.pawn.Get(), playing, services.encounter_proof))
            {
                services.compat_error = std::move(*error);
            }

            scene_hero_visibility.tick(pawn, playing);
            if (is_uobject_valid_fast(event_pawn) && (!is_uobject_valid_fast(scene) || !flag(scene, L"bIsPlaying")) &&
                !flag(event_pawn, L"bHidden"))
            {
                ReflectedCall hide(event_pawn, L"SetActorHiddenInGame");
                hide.boolean(L"bNewHidden", true);
                hide.run();
            }
            return;
        }
        try
        {
            if (!kh02_mirror_source_levels_ready(current_world, script, services.map_session))
            {
                return;
            }
            auto* source_script = script.Get();
            if (!is_uobject_valid_fast(source_script))
            {
                throw std::runtime_error("kh02_within_script_missing");
            }
            if (!services.event_aqua.prepare(source_script))
            {
                return;
            }
            if (!horizon.prepare(pawn))
            {
                return;
            }
            ReflectedCall hidden(services.event_aqua.actor(), L"SetActorHiddenInGame");
            hidden.boolean(L"bNewHidden", true);
            hidden.run();
            initialized = true;
            services.entry.initialized = true;
            // The source first-entry event requests DW016 and owns its actual
            // playback/continuation. Do not dispatch a synthetic completion.
            const auto arrival = kh02_mirror_arrival(services.resume_entry, services.arrival_tag);
            const auto& event = arrival.event;
            ReflectedCall init(source_script, event.c_str());
            if (arrival.common_entry_required)
            {
                // Clear-return events only notify the source camera script.
                // The source persistent level normally handles common entry
                // separately; the host must still run its playable setup.
                ReflectedCall common(source_script, L"dw_03_PersistentLevel_REvt_LevelStart");
                common.run();
                services.map_session.proof()["common_entry_before_return"] = to_utf8(event);
            }
            init.run();
            if (arrival.host_entry_fade)
            {
                services.map_session.request_entry_fade(source_script);
            }
            intro_started = true;
            services.arrival_tag.clear();
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
    }
} // namespace kh3ap
