#pragma once
#include "kh02_gameplay_binding.h"
#include "kh02_forest_handoff.h"
#include "kh3ap/features/kh02/world/kh02_mirror_horizon.h"
#include "kh3ap/features/kh02/world/kh02_entrance_mirror.h"
#include "kh3ap/features/kh02/world/kh02_forest_visibility.h"
#include "kh3ap/features/kh02/cutscenes/kh02_scene_hero_visibility.h"
#include <nlohmann/json.hpp>
#include <string>
#include <chrono>

namespace kh3ap
{
    class Kh02EntrySession;
    class Kh02EventAqua;
    class Kh02EnemyStats;
    class Kh02MapSession;

    struct Kh02MirrorGameplaySession
    {
        struct Services
        {
            Kh02EntrySession& entry;
            Kh02GameplayBinding& gameplay;
            Kh02EventAqua& event_aqua;
            Kh02EnemyStats& enemy_stats;
            Kh02MapSession& map_session;
            RC::Unreal::FWeakObjectPtr& postboss_matinee;
            nlohmann::json& encounter_proof;
            std::string& compat_error;
            bool& resume_entry;
            std::wstring& arrival_tag;
        };

        void begin_hub(RC::Unreal::UObject* current_world, Services services);
        void begin_room(RC::Unreal::UObject* current_world, RC::Unreal::UObject* source_initializer, Services services);
        void finish_initialization(RC::Unreal::UObject* context, Services services);
        void tick(RC::Unreal::UObject* pawn, RC::Unreal::UObject* current_world, Services services);
        void source_entrance_finished(RC::Unreal::UObject* source_script);
        void finish_entrance_view(RC::Unreal::UObject* pawn, bool scene_playing, Services services);
        RC::Unreal::FWeakObjectPtr world, script, initializer;
        RC::Unreal::FWeakObjectPtr entrance_finish_script;
        bool entrance_reveal_requested{};
        std::chrono::steady_clock::time_point entrance_reveal_started{};
        bool initialized{}, intro_started{};
        std::string error;
        Kh02MirrorHorizon horizon;
        Kh02EntranceMirror entrance_mirror;
        Kh02SceneHeroVisibility scene_hero_visibility;
        Kh02ForestHandoff forest_handoff;
        Kh02ForestVisibility forest_visibility;
        nlohmann::json forest_story_proof = nlohmann::json::object();
        std::chrono::steady_clock::time_point next_guard{};
        uint64_t unsupported_reactions_disabled{};
    };
} // namespace kh3ap
