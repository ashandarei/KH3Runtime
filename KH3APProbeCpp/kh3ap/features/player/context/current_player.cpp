#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/player/context/current_player.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using RC::Unreal::UObject;

    namespace
    {
        struct CurrentPlayerContext
        {
            RC::Unreal::FWeakObjectPtr pawn, controller, local_player, world;
            std::uint64_t revision{};
        } current_player;
        ReflectedPropertyCache<4> player_properties;
    }

    void runtime_clear_current_player()
    {
        // Invalidate even during an unfinished handoff; no old pawn may be
        // rediscovered from its still-live controller during map travel.
        const auto revision = current_player.revision + 1;
        current_player = {};
        current_player.revision = revision;
    }

    std::uint64_t runtime_current_player_revision()
    {
        return current_player.revision;
    }

    bool runtime_observe_current_player(UObject* pawn)
    {
        if (character_swap::handoff_pending() || !is_uobject_valid_fast(pawn) ||
            !pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID"))
        {
            return false; // Space vehicles and title/NPC controllers are separate.
        }
        auto* world = kh02_current_world();
        if (!is_uobject_valid_fast(world) || kh02_actor_world(pawn) != world) { return false; }
        auto* controller = player_properties.read_object(pawn, L"Controller");
        auto* local_player = player_properties.read_object(controller, L"Player");
        if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(local_player) ||
            kh02_actor_world(controller) != world ||
            player_properties.read_object(controller, L"Pawn") != pawn ||
            player_properties.read_object(local_player, L"PlayerController") != controller)
        {
            return false;
        }
        if (current_player.pawn.Get() != pawn || current_player.controller.Get() != controller ||
            current_player.local_player.Get() != local_player || current_player.world.Get() != world)
        {
            current_player.pawn = make_uobject_weak(pawn);
            current_player.controller = make_uobject_weak(controller);
            current_player.local_player = make_uobject_weak(local_player);
            current_player.world = make_uobject_weak(world);
            ++current_player.revision;
        }
        return true;
    }

    UObject* runtime_current_player_pawn()
    {
        if (character_swap::handoff_pending()) { return nullptr; }
        // Only follow the already observed LocalPlayer. This is an ownership
        // check at a caller's point of use, not idle UObject discovery.
        auto* controller = player_properties.read_object(current_player.local_player.Get(), L"PlayerController");
        auto* pawn = player_properties.read_object(controller, L"Pawn");
        if (!runtime_observe_current_player(pawn)) { return nullptr; }
        return pawn;
    }

    UObject* runtime_ready_player_pawn()
    {
        auto* pawn = runtime_current_player_pawn();
        if (!pawn || !runtime_sora_control_ready(pawn)) { return nullptr; }
        try
        {
            Kh02ReflectedCall loading(L"/Script/TresGame.Default__TresGameBlueprintLibrary",
                                     L"TresUI_IsClosedLoadingScreen", "current_player", 4096);
            loading.run();
            Kh02ReflectedCall paused(L"/Script/Engine.Default__GameplayStatics",
                                    L"IsGamePaused", "current_player", 4096);
            paused.object(L"WorldContextObject", pawn);
            paused.run();
            return loading.boolean_result() && !paused.boolean_result() ? pawn : nullptr;
        }
        catch (...) { return nullptr; }
    }
}
