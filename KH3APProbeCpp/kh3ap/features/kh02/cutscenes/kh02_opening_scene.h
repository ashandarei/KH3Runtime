#pragma once
#include "kh3ap/features/kh02/story/kh02_entry_session.h"
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    class Kh02EventAqua;
    class Kh02IntroPresentation;
    class Kh02StoryMusic;
    class Kh02CutsceneSession;

    class Kh02OpeningScene
    {
      public:
        // Borrowed only for the synchronous call. Stored scene objects use weak references.
        struct Context
        {
            RC::Unreal::UObject* gameplay_world{};
            RC::Unreal::UObject* gameplay_script{};
            bool gameplay{};
            bool initialized{};
            bool first_castle_intro{};
            std::function<void()> notify_ready;
        };

        Kh02OpeningScene(Kh02EventAqua& aqua, Kh02IntroPresentation& presentation, Kh02StoryMusic& music,
                         Kh02CutsceneSession& cutscenes);
        auto bind_context(RC::Unreal::UObject*& pawn, const Context& context) -> RC::Unreal::UObject*;
        auto prepare(RC::Unreal::UObject* pawn, const Context& context) -> bool;
        auto unload(RC::Unreal::UObject* pawn, const Context& context) -> bool;
        void start(RC::Unreal::UObject* pawn, const Context& context);
        void skip(RC::Unreal::UObject* pawn, const Context& context);
        void observe(RC::Unreal::UObject* current_world);
        auto observation() const -> Kh02EntrySession::SceneObservation;

        auto source_owner() const -> RC::Unreal::UObject*
        {
            return owner.Get();
        }

        auto source_stream() const -> RC::Unreal::UObject*
        {
            return stream.Get();
        }

        auto source_script() const -> RC::Unreal::UObject*
        {
            return script.Get();
        }

        auto source_matinee() const -> RC::Unreal::UObject*
        {
            return matinee.Get();
        }

        auto was_dispatched() const -> bool
        {
            return dispatched;
        }

      private:
        void require_loaded_scene() const;
        Kh02EventAqua& event_aqua;
        Kh02IntroPresentation& intro_presentation;
        Kh02StoryMusic& music;
        Kh02CutsceneSession& cutscenes;
        RC::Unreal::FWeakObjectPtr owner, world, stream, script, matinee;
        bool dispatched{};
        bool observed_playing{};
        float maximum_position{};
    };
} // namespace kh3ap
