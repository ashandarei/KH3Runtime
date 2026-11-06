#pragma once
#include "kh02_session_retirement_policy.h"

namespace RC::Unreal
{
    class UObject;
    struct FWeakObjectPtr;
} // namespace RC::Unreal

namespace kh3ap
{
    class Kh02MapSession;
    class Kh02MovieBridge;
    class Kh02CutsceneSession;
    class Kh02HelpContinuations;
    class Kh02InformationQueue;
    class Kh02MirrorTravel;
    class Kh02PostBossHandoff;
    struct Kh02MirrorGameplaySession;
    struct Kh02CastleGameplaySession;
    class Kh02EntrySession;
    class Kh02ChallengeLog;
    class Kh02DressingRoom;
    class Kh02IntroPresentation;
    class Kh02TimedInputs;
    class Kh02EnemyStats;
    class Kh02ChallengeTracker;
    class Kh02Hud;
    class Kh02StoryTargets;
    class Kh02IslandMaterials;
    class Kh02ChestObserver;
    class Kh02NativeMovieProbe;
    struct Kh02GameplayBinding;
    class Kh02MeshBufferLease;
    class Kh02EntryAssets;
    class Kh02AbilityCapacity;
    class Kh02DarksideProjectileRepair;
    class Kh02DarksidePillarRepair;

    // Borrowed synchronously. Domain owners keep their state and lifetime; this
    // coordinator owns only the reason-specific release order and error policy.
    struct Kh02SessionRetirement
    {
        Kh02MapSession& map_session;
        Kh02MovieBridge& movie;
        Kh02CutsceneSession& cutscenes;
        Kh02HelpContinuations& help;
        Kh02InformationQueue& information;
        Kh02MirrorTravel& travel;
        Kh02PostBossHandoff& postboss;
        Kh02MirrorGameplaySession& mirror;
        Kh02CastleGameplaySession& castle;
        Kh02EntrySession& entry;
        Kh02ChallengeLog& challenges;
        Kh02DressingRoom& wardrobe;
        Kh02IntroPresentation& intro;
        Kh02TimedInputs& inputs;
        Kh02EnemyStats& enemies;
        Kh02ChallengeTracker& challenge_tracker;
        Kh02Hud& hud;
        Kh02StoryTargets& targets;
        Kh02IslandMaterials& island_materials;
        Kh02ChestObserver& chests;
        Kh02NativeMovieProbe& native_movie;
        Kh02GameplayBinding& gameplay;
        RC::Unreal::FWeakObjectPtr& entry_world;
        Kh02MeshBufferLease& mesh_buffers;
        Kh02EntryAssets& entry_assets;
        Kh02AbilityCapacity& ability_capacity;
        Kh02DarksideProjectileRepair& darkside_projectiles;
        Kh02DarksidePillarRepair& darkside_pillars;

        auto run(Kh02RetirementReason reason, RC::Unreal::UObject* destination = nullptr) -> std::string;

      private:
        void execute(Kh02RetirementAction action, Kh02RetirementReason reason, RC::Unreal::UObject* destination);
        void clear_world_bindings();
    };
} // namespace kh3ap
