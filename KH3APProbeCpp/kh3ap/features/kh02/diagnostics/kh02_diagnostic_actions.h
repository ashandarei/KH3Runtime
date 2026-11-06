#pragma once
#include "kh02_diagnostic_action.h"
#include "kh3ap/features/kh02/cutscenes/kh02_opening_scene.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>

namespace kh3ap
{
    struct Kh02CastleGameplaySession;
    class Kh02ChallengeLog;
    class Kh02CutsceneSession;
    class Kh02EntrySession;
    class Kh02EventAqua;
    struct Kh02GameplayBinding;
    class Kh02Localization;
    class Kh02MapSession;
    class Kh02MeshBufferLease;
    struct Kh02MirrorGameplaySession;
    class Kh02MovieBridge;
    class Kh02NativeMovieProbe;
    class Kh02StoryFlags;
    class Kh02StoryTargets;
    class Kh02TimedInputs;

    // Borrowed per command. Snapshots are evaluated after mutations, while the
    // native alias installation service remains owned by the story coordinator.
    struct Kh02CastleActionServices
    {
        Kh02EntrySession& entry;
        Kh02GameplayBinding& gameplay;
        Kh02CastleGameplaySession& castle;
        Kh02MapSession& map_session;
        Kh02ChallengeLog& challenge_log;
        Kh02MovieBridge& movie_bridge;
        Kh02StoryFlags& source_flags;
        Kh02TimedInputs& timed_inputs;
        std::map<int, int>& within_challenges;
        Kh02StoryTargets& story_targets;
        Kh02MeshBufferLease& mesh_buffers;
        Kh02Localization& localization;
        nlohmann::json& native_before;
        std::string& compat_error;
        std::string& compat_warning;
        nlohmann::json& encounter_proof;
        bool& movie_fixture;
        std::function<void()> install_compat;
        std::function<nlohmann::json()> snapshot;
    };

    struct Kh02MovieActionServices
    {
        Kh02GameplayBinding& gameplay;
        Kh02CutsceneSession& cutscenes;
        Kh02MovieBridge& movie_bridge;
        Kh02NativeMovieProbe& native_movie;
        bool& movie_fixture;
        bool& native_movie_fixture;
        nlohmann::json& movie_calls;
    };

    struct Kh02MirrorActionServices
    {
        Kh02MirrorGameplaySession& mirror;
        Kh02StoryFlags& source_flags;
        Kh02TimedInputs& timed_inputs;
        RC::Unreal::FWeakObjectPtr& postboss_matinee;
        std::function<nlohmann::json()> snapshot;
    };

    struct Kh02OpeningActionServices
    {
        Kh02OpeningScene& opening_scene;
        Kh02EventAqua& event_aqua;
        Kh02OpeningScene::Context context;
        std::function<nlohmann::json()> snapshot;
    };

    class Kh02CastleDiagnosticActions
    {
      public:
        static auto execute(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, Kh02DiagnosticAction action,
                            Kh02CastleActionServices services) -> std::optional<std::string>;
    };

    class Kh02MovieDiagnosticActions
    {
      public:
        static void execute(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, Kh02DiagnosticAction action,
                            Kh02MovieActionServices services);
    };

    class Kh02MirrorDiagnosticActions
    {
      public:
        static auto execute(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, Kh02DiagnosticAction action,
                            Kh02MirrorActionServices services) -> std::optional<std::string>;
    };

    class Kh02OpeningDiagnosticActions
    {
      public:
        static auto execute(RC::Unreal::UObject* pawn, Kh02DiagnosticAction action, Kh02OpeningActionServices services)
            -> std::optional<std::string>;
    };
} // namespace kh3ap
