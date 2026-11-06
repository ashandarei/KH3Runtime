#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace kh3ap
{
    class Kh02EventAqua;
    class Kh02CutsceneSession;
    class Kh02OpeningFollowup;
    class Kh02PostBossHandoff;

    // Coordinates scene presentation binding without retaining another copy of
    // story ownership. Every service is borrowed for one synchronous call.
    class Kh02EncounterSceneBinding
    {
      public:
        struct Services
        {
            const RC::Unreal::FWeakObjectPtr& world;
            const RC::Unreal::FWeakObjectPtr& pawn;
            Kh02EventAqua& event_aqua;
            Kh02CutsceneSession& cutscenes;
            Kh02OpeningFollowup& opening_followup;
            Kh02PostBossHandoff& postboss;
            nlohmann::json& proof;
            std::string& error;
        };

        // Preparation errors still allow the binding attempt. The caller invokes
        // the original native thunk afterward, outside both catch boundaries.
        static void prepare_and_bind(RC::Unreal::UObject* object, const Services& services);
        static void bind(RC::Unreal::UObject* object, const Services& services);
        // Called only after the native override accepts its alternate actor site.
        // That route deliberately uses group 0 and its supplied scene flag.
        static void bind_override(RC::Unreal::UObject* object, bool scene, const Services& services);
        static void bind_aqua(RC::Unreal::UObject* object, const wchar_t* group, const Services& services);

      private:
        static void bind_and_show(RC::Unreal::UObject* object, const wchar_t* group, const Services& services);
    };
} // namespace kh3ap
