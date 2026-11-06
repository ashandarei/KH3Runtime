#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <cstdint>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct Kh02CastleGameplaySession;
    class Kh02ChallengeLog;
    class Kh02ChallengeTracker;
    class Kh02ChestObserver;
    class Kh02CutsceneSession;
    class Kh02DressingRoom;
    class Kh02EnemyStats;
    class Kh02EntrySession;
    class Kh02EventAqua;
    struct Kh02GameplayBinding;
    class Kh02Hud;
    class Kh02InformationQueue;
    class Kh02IntroPresentation;
    class Kh02IslandMaterials;
    class Kh02Localization;
    class Kh02MapSession;
    class Kh02MeshBufferLease;
    struct Kh02MirrorGameplaySession;
    class Kh02MovieBridge;
    class Kh02NativeMovieProbe;
    class Kh02OpeningFollowup;
    class Kh02OpeningScene;
    class Kh02ProgressSession;
    class Kh02StoryFlags;
    class Kh02StoryMusic;
    class Kh02StoryTargets;

    // Synchronous borrowed views only. Diagnostics retain no world objects or callbacks.
    struct Kh02StoryDiagnosticsCommon
    {
        Kh02StoryFlags& source_flags;
        Kh02Hud& hud;
        Kh02EnemyStats& enemy_stats;
        Kh02ChallengeTracker& challenge_tracker;
        Kh02ChestObserver& chest_observer;
        Kh02ProgressSession& progress_session;
        Kh02DressingRoom& dressing_room;
        Kh02ChallengeLog& challenge_log;
        Kh02MapSession& map_session;
        Kh02StoryTargets& story_targets;
        Kh02StoryMusic& music;
        Kh02CutsceneSession& cutscenes;
        Kh02MovieBridge& movie_bridge;
        Kh02MeshBufferLease& mesh_buffers;
    };

    struct Kh02FlowDiagnosticsView
    {
        Kh02StoryDiagnosticsCommon common;
        Kh02GameplayBinding& gameplay;
        Kh02CastleGameplaySession& castle;
        Kh02EntrySession& entry;
        Kh02Localization& localization;
        Kh02NativeMovieProbe& native_movie;
        nlohmann::json compatibility;
        const nlohmann::json& native_before;
        const nlohmann::json& encounter_proof;
        const nlohmann::json& movie_calls;
        const std::map<int, int>& within_challenge_counts;
        const std::map<int, int>& within_challenges;
        bool challenges_active{};
        bool postboss_observed{};
        float postboss_maximum{};
        bool story_travel_requested{};
        bool native_movie_fixture{};
    };

    struct Kh02OpeningDiagnosticsView
    {
        Kh02OpeningScene& opening_scene;
        Kh02EventAqua& event_aqua;
        Kh02EntrySession& entry;
        Kh02IntroPresentation& intro_presentation;
        Kh02OpeningFollowup& opening_followup;
    };

    struct Kh02WithinDiagnosticsView
    {
        Kh02StoryDiagnosticsCommon common;
        Kh02MirrorGameplaySession& mirror;
        Kh02InformationQueue& information;
        Kh02IslandMaterials& island_materials;
        const std::string& compat_error;
        const std::string& compat_warning;
        uint64_t mapset_calls{};
        const nlohmann::json& last_mapset;
        const nlohmann::json& encounter_proof;
    };

    class Kh02StoryDiagnostics
    {
      public:
        static auto flow_snapshot(RC::Unreal::UObject* pawn, const Kh02FlowDiagnosticsView& view) -> nlohmann::json;
        static auto opening_snapshot(const Kh02OpeningDiagnosticsView& view) -> nlohmann::json;
        static auto within_snapshot(RC::Unreal::UObject* pawn, const Kh02WithinDiagnosticsView& view) -> nlohmann::json;
    };
} // namespace kh3ap
