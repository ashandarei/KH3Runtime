#include "kh02_session_retirement.h"
#include "kh02_map_session.h"
#include "kh02_help_continuations.h"
#include "kh02_mirror_travel.h"
#include "kh02_postboss_handoff.h"
#include "kh02_mirror_gameplay_session.h"
#include "kh02_castle_gameplay_session.h"
#include "kh02_entry_session.h"
#include "kh02_timed_inputs.h"
#include "kh02_story_targets.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include "kh3ap/features/kh02/movies/kh02_movie_probe.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_session.h"
#include "kh3ap/features/kh02/cutscenes/kh02_intro_presentation.h"
#include "kh3ap/features/kh02/ui/kh02_information_queue.h"
#include "kh3ap/features/kh02/ui/kh02_hud.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_tracker.h"
#include "kh3ap/features/kh02/wardrobe/kh02_dressing_room.h"
#include "kh3ap/features/kh02/combat/kh02_enemy_stats.h"
#include "kh3ap/features/kh02/combat/kh02_darkside_pillar_repair.h"
#include "kh3ap/features/kh02/combat/kh02_darkside_projectile_repair.h"
#include "kh3ap/features/kh02/world/kh02_island_materials.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_observer.h"
#include "kh3ap/features/kh02/core/kh02_mesh_buffer_lease.h"
#include "kh3ap/features/kh02/runtime/kh02_entry_assets.h"
#include "kh3ap/features/kh02/rewards/kh02_ability_capacity.h"
#include <cstdint>

namespace kh3ap
{
    auto Kh02SessionRetirement::run(Kh02RetirementReason reason, RC::Unreal::UObject* destination) -> std::string
    {
        return kh02_run_retirement(reason, [&](Kh02RetirementAction action) { execute(action, reason, destination); });
    }

    void Kh02SessionRetirement::execute(Kh02RetirementAction action, Kh02RetirementReason reason,
                                        RC::Unreal::UObject* destination)
    {
        using Action = Kh02RetirementAction;
        switch (action)
        {
        case Action::DeactivateEnemies:
            enemies.set_active(false);
            break;
        case Action::ObserveDestination:
            map_session.observe_world(destination);
            break;
        case Action::CancelArrival:
            map_session.cancel();
            break;
        case Action::InvalidateSource:
            map_session.invalidate_source_script();
            break;
        case Action::RetireMovie:
            movie.retire_session();
            break;
        case Action::CancelCutscenes:
            cutscenes.cancel();
            break;
        case Action::ClearHelp:
            help.clear();
            break;
        case Action::ClearInformation:
            information.clear();
            break;
        case Action::CancelTravel:
            travel.cancel();
            break;
        case Action::CancelPostbossExit:
            postboss.state.cancel_exit();
            break;
        case Action::ClearForestHandoff:
            mirror.forest_handoff.clear();
            break;
        case Action::ClearDarksideProjectiles:
            darkside_projectiles.reset();
            darkside_pillars.reset();
            break;
        case Action::DisarmCastle:
            castle.initializer = {};
            castle.armed = false;
            break;
        case Action::RetireEntry:
            entry.retire();
            break;
        case Action::UnbindChallenges:
            challenge_tracker.bind(nullptr, nullptr, UINT64_MAX);
            break;
        case Action::HideHud:
            hud.tick(nullptr, false);
            hud.reset_world();
            break;
        case Action::CloseChallenges:
            challenges.close();
            break;
        case Action::ReleaseWardrobe:
            wardrobe.release(reason == Kh02RetirementReason::Failure);
            break;
        case Action::RestoreHud:
            intro.set_hud_hidden(false);
            break;
        case Action::CancelInputs:
            inputs.cancel();
            break;
        case Action::ClearWorldBindings:
            clear_world_bindings();
            break;
        case Action::RestoreMeshBuffers:
            mesh_buffers.restore(destination);
            break;
        case Action::ReleaseEntryAssets:
            ability_capacity.release();
            entry_assets.release();
            break;
        }
    }

    void Kh02SessionRetirement::clear_world_bindings()
    {
        targets.clear_world();
        mirror.forest_visibility.clear_world();
        castle.shooting_star.clear_world();
        castle.path_trigger.clear_world();
        island_materials.clear_world();
        mirror.entrance_mirror.clear();
        mirror.entrance_finish_script = {};
        chests.clear_world();
        native_movie.clear_world();
        gameplay.pawn = {};
        gameplay.world = {};
        mirror.world = {};
        entry_world = {};
        castle.initializer = {};
        mirror.initializer = {};
        castle.armed = false;
        entry.initialized = false;
        entry.retire();
    }
} // namespace kh3ap
