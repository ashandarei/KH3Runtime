#include "kh02_cutscene_session.h"
#include "kh02_ending_subtitles.h"
#include "kh02_collapse_presentation.h"
#include "kh3ap/features/kh02/movies/kh02_movie_actor_visibility.h"
#include "kh02_ending_voice.h"
#include "kh02_event_aqua_scenes.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <exception>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;
    struct Kh02CutsceneSession::Impl
    {
        Kh02MovieBridge& movie_bridge;
        Hooks hooks;
        std::string presentation_error;

        Impl(Kh02MovieBridge& movies, Hooks callbacks) : movie_bridge(movies), hooks(std::move(callbacks)) {}

        Kh02CollapsePresentation collapse;
        Kh02EndingSubtitles subtitles;
        Kh02MovieActorVisibility movie_actor_visibility;

        Kh02EndingVoice ending_voice;
        Json ending_presentation = Json::object();

        void prepare_ending_presentation(UObject* matinee, UObject* world, UObject* pawn)
        {
            if (!is_uobject_valid_fast(matinee) || kh02_actor_world(matinee) != world)
            {
                return;
            }
            if (!hooks.full_movie_for_scene(matinee))
            {
                return;
            }
            hooks.prepare_localization();
            subtitles.prepare(matinee, ending_presentation);
            if (subtitles.is_full_movie(matinee))
            {
                movie_actor_visibility.collect(matinee, pawn);
                // Metadata selects movie PCM or localized Matinee dialogue.
                // Authored BGM remains separate from either audio owner.
            }
            ending_presentation["full_movie"] = subtitles.is_full_movie(matinee);
            ending_presentation["movie_disabled_tracks"] = subtitles.disabled_tracks();
            ending_presentation["movie_hidden_actors"] = movie_actor_visibility.size();
            ending_presentation["mickey_text"] = hooks.localized_message(L"kh02_event_dw.dw033", L"a0020", nullptr);
            ending_presentation["aqua_text"] = hooks.localized_message(L"kh02_event_dw.dw034", L"a0060", nullptr);

            ending_voice.prepare(matinee, ending_presentation);
        }

        Kh02EventAquaScenes event_aqua_scenes;

        void cancel() noexcept
        {
            presentation_error.clear();
            subtitles.retire_presentation();
            movie_actor_visibility.cancel();
            ending_voice.restore();
            subtitles.restore_tracks();
            collapse.restore();
            event_aqua_scenes.cancel();
            subtitles.reset();
            collapse.reset();
        }
    };

    Kh02CutsceneSession::Kh02CutsceneSession(Kh02MovieBridge& movies, Hooks hooks)
        : impl(std::make_unique<Impl>(movies, std::move(hooks)))
    {
    }

    Kh02CutsceneSession::~Kh02CutsceneSession()
    {
        cancel();
        impl->ending_voice.release_root();
    }

    void Kh02CutsceneSession::select_full_movie(UObject* matinee, std::string logical)
    {
        impl->subtitles.select_full_movie(matinee, std::move(logical));
    }

    void Kh02CutsceneSession::prepare(UObject* matinee, UObject* world, UObject* pawn)
    {
        impl->presentation_error.clear();
        try
        {
            impl->prepare_ending_presentation(matinee, world, pawn);
        }
        catch (const std::exception& error)
        {
            cancel();
            impl->presentation_error = error.what();
            throw;
        }
        catch (...)
        {
            cancel();
            impl->presentation_error = "kh02_cutscene_unknown_prepare_failure";
            throw;
        }
    }

    void Kh02CutsceneSession::track_event_aqua(UObject* matinee, UObject* event_aqua)
    {
        impl->event_aqua_scenes.track(matinee, event_aqua);
    }

    void Kh02CutsceneSession::bind_collapse(UObject* matinee, UObject* script)
    {
        impl->presentation_error.clear();
        impl->collapse.bind(matinee, script);
    }

    void Kh02CutsceneSession::tick_collapse(UObject* pawn, UObject* world, UObject* event_aqua)
    {
        try
        {
            impl->collapse.tick(pawn, world, event_aqua, impl->presentation_error);
        }
        catch (const std::exception& error)
        {
            cancel();
            impl->presentation_error = error.what();
            throw;
        }
    }

    void Kh02CutsceneSession::tick(UObject* pawn, UObject* world, UObject* event_aqua)
    {
        try
        {
            impl->collapse.tick(pawn, world, event_aqua, impl->presentation_error);
            impl->event_aqua_scenes.tick();
            impl->subtitles.tick(world, impl->movie_bridge, impl->hooks.localized_message, impl->ending_presentation);
            impl->movie_actor_visibility.tick(world);
        }
        catch (const std::exception& error)
        {
            cancel();
            impl->presentation_error = error.what();
            throw;
        }
        catch (...)
        {
            cancel();
            impl->presentation_error = "kh02_cutscene_unknown_update_failure";
            throw;
        }
    }

    void Kh02CutsceneSession::cancel() noexcept
    {
        impl->cancel();
    }

    auto Kh02CutsceneSession::needs_frame() const -> bool
    {
        return impl->event_aqua_scenes.needs_frame() || impl->collapse.needs_frame() || impl->subtitles.needs_frame() ||
               impl->movie_actor_visibility.needs_frame();
    }

    auto Kh02CutsceneSession::failure() const -> const std::string&
    {
        return impl->presentation_error;
    }

    auto Kh02CutsceneSession::event_aqua_playing() const -> bool
    {
        return impl->event_aqua_scenes.playing();
    }

    auto Kh02CutsceneSession::collapse_actor() const -> UObject*
    {
        return impl->collapse.actor();
    }

    auto Kh02CutsceneSession::presentation_snapshot() const -> Json
    {
        return impl->ending_presentation;
    }

    auto Kh02CutsceneSession::event_aqua_snapshot() const -> Json
    {
        return impl->event_aqua_scenes.snapshot();
    }

    auto Kh02CutsceneSession::collapse_snapshot() const -> Json
    {
        return impl->collapse.snapshot();
    }
} // namespace kh3ap
