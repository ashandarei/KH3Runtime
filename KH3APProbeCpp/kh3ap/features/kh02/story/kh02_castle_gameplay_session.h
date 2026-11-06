#pragma once
#include "kh02_gameplay_binding.h"
#include "kh3ap/features/kh02/world/kh02_castle_path.h"
#include "kh3ap/features/kh02/world/kh02_shooting_star.h"
#include <nlohmann/json.hpp>
#include <vector>

namespace kh3ap
{
    class Kh02EntrySession;
    class Kh02StoryFlags;
    class Kh02EventAqua;
    class Kh02MapSession;

    struct Kh02CastleGameplaySession
    {
        struct Services
        {
            Kh02EntrySession& entry;
            Kh02GameplayBinding& gameplay;
            Kh02StoryFlags& source_flags;
            Kh02EventAqua& event_aqua;
            Kh02MapSession& map_session;
            nlohmann::json& encounter_proof;
        };

        void begin(RC::Unreal::UObject* world, Kh02EntrySession& entry, Kh02GameplayBinding& gameplay);
        auto finish(Services services) -> bool;

        // The coordinator also sets these when preparing travel or a native retry.
        bool armed{}, loading_held{}, mapset_requested{}, retry{}, backtrack{}, backtrack_pending{};
        RC::Unreal::FWeakObjectPtr preload_pawn, gimmick_script, initializer;
        std::vector<RC::Unreal::FWeakObjectPtr> streams;
        Kh02CastlePath path_trigger;
        Kh02ShootingStar shooting_star;
        nlohmann::json proof = nlohmann::json::object();
    };
} // namespace kh3ap
