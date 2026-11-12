#include "character_reactions.h"
#include "character_reaction_policy.h"
#include "character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>

namespace kh3ap::character_reactions
{
    using namespace RC::Unreal;
    namespace
    {
        UObject* object(UObject* owner, const wchar_t* name)
        {
            return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, name) : nullptr;
        }

        bool boolean(UObject* owner, const wchar_t* name, bool wanted)
        {
            auto* property = is_uobject_valid_fast(owner) ? owner->GetPropertyByNameInChain(name) : nullptr;
            return property && property->IsA<FBoolProperty>() &&
                   static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(owner) == wanted;
        }
    }

    bool owns_command(UObject* controller, int command)
    {
        switch (static_cast<PlayerActionCommand>(command))
        {
        case PlayerActionCommand::TALK:
        case PlayerActionCommand::CHECK:
        case PlayerActionCommand::RESCUE:
        case PlayerActionCommand::VEHICLE:
        case PlayerActionCommand::TRICK:
        case PlayerActionCommand::COVERSLIDE:
            break;
        default:
            return false;
        }
        auto* pawn = object(controller, L"Pawn");
        if (!pawn || character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn) ||
            !character_traversal::ready(pawn) || object(pawn, L"Controller") != controller ||
            object(object(controller, L"Player"), L"PlayerController") != controller)
        {
            return false;
        }
        if (command == player_action_command_value(PlayerActionCommand::COVERSLIDE))
        {
            // The native command is offered while approaching cover in Idle
            // or Run, and queues Cover with the controller's selected destination.
            // Sora's dispatcher retains native eligibility and direction checks.
            auto* state = object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (!is_uobject_valid_fast(state)) { return false; }
            const auto path = state->GetClassPrivate()->GetPathName();
            return path == L"/Script/TresGame.TresPlayerState_Idle" ||
                   path == L"/Script/TresGame.TresPlayerState_Run" ||
                   path == L"/Script/TresGame.TresPlayerState_Cover";
        }
        auto* info = object(controller, L"m_pUICommandInfo");
        auto* actor = object(info, L"m_Actor");
        auto* reactor = object(info, L"m_Component");
        auto* type = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresReactorComponent");
        auto* world = kh02_actor_world(pawn);
        if (!world || !is_uobject_valid_fast(actor) || kh02_actor_world(actor) != world ||
            !is_uobject_valid_fast(reactor) || !is_uobject_valid_fast(type) || !is_uobject_a_fast(reactor, type) ||
            reactor->GetOuterPrivate() != actor || !boolean(reactor, L"m_bIsGimmickActive", true) ||
            !boolean(actor, L"bHidden", false))
        {
            return false;
        }
        auto* selected = reactor->GetPropertyByNameInChain(L"m_Command");
        if (!selected || selected->GetElementSize() != sizeof(uint8_t) ||
            !matches_reactor(command, *selected->ContainerPtrToValuePtr<uint8_t>(reactor)))
        {
            return false;
        }
        if (command == player_action_command_value(PlayerActionCommand::TRICK))
        {
            return character_barrel_spin::selected_barrel(actor, reactor, pawn);
        }
        if (command == player_action_command_value(PlayerActionCommand::VEHICLE))
        {
            auto* gigas = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresVehiclePawnGigas");
            return is_uobject_valid_fast(gigas) && is_uobject_a_fast(actor, gigas) &&
                   object(actor, L"m_Reactor") == reactor;
        }
        // Sora's selected-command route still checks native eligibility and
        // dispatches the original actor's callback. No synthetic activation,
        // actor discovery, story flag write, or completion is performed here.
        return true;
    }
}
