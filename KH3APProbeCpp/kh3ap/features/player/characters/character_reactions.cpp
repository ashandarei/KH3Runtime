#include "character_reactions.h"
#include "character_reaction_policy.h"
#include "character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <stdexcept>

namespace kh3ap::character_reactions
{
    using namespace RC::Unreal;
    namespace
    {
        FWeakObjectPtr sora_defaults;

        template<class T>
        void fill_tuning(UObject* pawn, UObject* source, const wchar_t* name, const wchar_t* kind)
        {
            auto* field = source->GetPropertyByNameInChain(name);
            if (!field || field != pawn->GetPropertyByNameInChain(name) ||
                field->GetClass().GetName() != kind || field->GetArrayDim() != 1 ||
                field->GetElementSize() != sizeof(T))
            {
                throw std::runtime_error("character_reaction_tuning_layout");
            }
            auto& destination = *field->ContainerPtrToValuePtr<T>(pawn);
            if (destination == T{}) { destination = *field->ContainerPtrToValuePtr<T>(source); }
        }

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

    void capture_sora(UObject* pawn)
    {
        sora_defaults = make_uobject_weak(is_uobject_valid_fast(pawn)
            ? pawn->GetClassPrivate()->GetClassDefaultObject() : nullptr);
    }

    void prepare_pawn(UObject* pawn)
    {
        auto* source = sora_defaults.Get();
        if (!source || !character_swap::owns_swap_pawn(pawn))
        {
            throw std::runtime_error("character_reaction_defaults_missing");
        }
        // Populate absent ride references only. Native drawing, availability,
        // async loading and command consumption remain responsible for activation.
        // Link slots and authored alternate ride references are left alone.
        auto* field = source->GetPropertyByNameInChain(L"m_AttractionFlowAssetInfo");
        if (!field || field != pawn->GetPropertyByNameInChain(L"m_AttractionFlowAssetInfo") ||
            !field->IsA<FStructProperty>() || field->GetArrayDim() != 16)
        {
            throw std::runtime_error("character_attraction_info_layout");
        }
        auto* schema = static_cast<FStructProperty*>(field)->GetStruct();
        auto* asset = schema->GetPropertyByNameInChain(L"m_AssetPtr");
        // KH3's UE4.17 asset-class reference stores its owned FString after
        // the weak-object/cache fields. This is the same legacy layout used
        // by the runtime's latent asset loader; never shallow-copy its string.
        if (!asset || asset->GetClass().GetName() != L"AssetClassProperty" ||
            asset->GetOffset_Internal() != 0 || asset->GetElementSize() != 0x10 + sizeof(FString) ||
            schema->GetStructureSize() != field->GetElementSize())
        {
            throw std::runtime_error("character_attraction_reference_layout");
        }
        for (int index : {0, 1, 2, 4, 5, 8})
        {
            auto* destination = field->ContainerPtrToValuePtr<std::byte>(pawn, index);
            const auto& path = *reinterpret_cast<FString*>(destination + 0x10);
            if (!path.GetCharArray() || !*path.GetCharArray())
            {
                schema->CopyScriptStruct(destination, field->ContainerPtrToValuePtr<void>(source, index));
            }
        }
        for (const auto* name : {L"MyFriendComboFinishRange", L"MyFriendComboFinishRangeHalf",
                                  L"MyFriendComboFinishRangeNear"})
        {
            fill_tuning<float>(pawn, source, name, L"FloatProperty");
        }
        for (const auto* name : {L"MyFriendComboFinishAddRate", L"MyFriendComboFinishAddRateRange",
                                  L"MyFriendComboFinishAddRateRangeHalf", L"MyFriendComboFinishAddRateRangeNear"})
        {
            fill_tuning<int32_t>(pawn, source, name, L"IntProperty");
        }
        fill_tuning<UObject*>(pawn, source, L"m_pCoverShotDestroyEffect", L"ObjectProperty");
    }

    bool owns_command(UObject* controller, int command)
    {
        auto* pawn = object(controller, L"Pawn");
        auto* player_field = pawn ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID") : nullptr;
        const bool situation = player_field && player_field->GetElementSize() == sizeof(uint8_t) &&
            shared_situation(command, static_cast<PlayerId>(*player_field->ContainerPtrToValuePtr<uint8_t>(pawn)));
        switch (static_cast<PlayerActionCommand>(command))
        {
        case PlayerActionCommand::TALK:
        case PlayerActionCommand::OPEN:
        case PlayerActionCommand::CHECK:
        case PlayerActionCommand::RESCUE:
        case PlayerActionCommand::VEHICLE:
        case PlayerActionCommand::PLANE:
        case PlayerActionCommand::PLANEBOSS:
        case PlayerActionCommand::ENEMYSHIP:
        case PlayerActionCommand::TRICK:
        case PlayerActionCommand::WATERPLAY:
        case PlayerActionCommand::EXTENDHAND:
        case PlayerActionCommand::SWING:
        case PlayerActionCommand::PUDDING:
        case PlayerActionCommand::POSTCARD:
        case PlayerActionCommand::CARDDRAW:
        case PlayerActionCommand::SLIDESHOT:
        case PlayerActionCommand::COVERSLIDE:
        case PlayerActionCommand::DISPEL:
        case PlayerActionCommand::QUICKBATTLE:
            break;
        default:
            if (!situation) { return false; }
            break;
        }
        if (!pawn || character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn) ||
            !character_traversal::ready(pawn) || object(pawn, L"Controller") != controller ||
            object(object(controller, L"Player"), L"PlayerController") != controller ||
            kh02_actor_world(pawn) != kh02_current_world())
        {
            return false;
        }
        if (situation)
        {
            // Native dispatch checks the selected command, availability, meter,
            // teammate/ride and state eligibility. Do not synthesize command-info
            // actors or grant attraction/team availability here.
            return true;
        }
        if (command == player_action_command_value(PlayerActionCommand::QUICKBATTLE))
        {
            // Sora's dispatcher calls the shared pawn's native eligibility and
            // QuickBattle request. The encounter still owns the offered command;
            // forwarding does not manufacture a prompt or complete the duel.
            return true;
        }
        if (command == player_action_command_value(PlayerActionCommand::DISPEL))
        {
            // Dispel is a Death Sentence command, not a TresReactorComponent.
            // Sora's dispatcher queries the shared pawn interface before clearing
            // the countdown and notifying its original owning encounter.
            return object(pawn, L"m_pDeathSentenceCountEffect") != nullptr;
        }
        const bool cover_shot = command == player_action_command_value(PlayerActionCommand::SLIDESHOT);
        if (cover_shot || command == player_action_command_value(PlayerActionCommand::COVERSLIDE))
        {
            // Cover Slide can start from Idle or Run. The native Cover state
            // offers Slide Shot only after it selects a lock-on target.
            auto* state = object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (!is_uobject_valid_fast(state)) { return false; }
            const auto path = state->GetClassPrivate()->GetPathName();
            return path == L"/Script/TresGame.TresPlayerState_Cover" ||
                   (!cover_shot && (path == L"/Script/TresGame.TresPlayerState_Idle" ||
                                   path == L"/Script/TresGame.TresPlayerState_Run"));
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
        if (command == player_action_command_value(PlayerActionCommand::OPEN))
        {
            auto* chest = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresTreasureBox");
            return is_uobject_valid_fast(chest) && is_uobject_a_fast(actor, chest) &&
                   object(actor, L"MyReactor") == reactor;
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
