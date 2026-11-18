#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include <limits>
#include <cstring>
#include <Unreal/FFrame.hpp>
#include "character_reactions.h"
#include "character_reaction_policy.h"
#include "character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <stdexcept>

namespace kh3ap::character_reactions
{
    using namespace RC::Unreal;
    namespace
    {
        FWeakObjectPtr sora_defaults;
        constexpr auto graveyard = L"/Game/Levels/kg/kg_01/kg_01.kg_01";
        constexpr auto remote_event = L"/Script/TresGame.TresGameBlueprintLibrary:RemoteEvent";
        constexpr auto enemy_type = L"/Game/Levels/kg/kg_01/umap/kg_01_enemy_01.kg_01_enemy_01_C";
        constexpr auto camera_type = L"/Game/Levels/kg/kg_01/umap/kg_01_camera_ms011.kg_01_camera_ms011_C";
        constexpr auto flow_type = L"/Game/Levels/kg/kg_01/umap/kg_01_gameflow.kg_01_gameflow_C";
        std::pair<int, int> event_hooks{};
        bool graveyard_portal_pending{};
        FWeakObjectPtr score_logged[2];

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

    void rebind_scripted_coaster(UObject* outgoing, UObject* incoming)
    {
        auto* world = actor_owning_world(incoming);
        if (!is_uobject_valid_fast(world) || world->GetPathName() != graveyard) { return; }
        auto* controller = object(incoming, L"Controller");
        auto* player = object(controller, L"Player");
        if (object(player, L"PlayerController") != controller || !controller || object(controller, L"Pawn") != incoming) { return; }
        auto* field = incoming->GetPropertyByNameInChain(L"OnPlayerSpecificAction");
        if (!field || !field->IsA<FMulticastDelegateProperty>() || field->GetElementSize() != sizeof(FMulticastScriptDelegate)) { return; }
        auto& listeners = field->ContainerPtrToValuePtr<FMulticastScriptDelegate>(incoming)->InvocationList;
        const FName callback(L"ReceivePlayerSpecificAction_Copy", FNAME_Add);
        auto bind = [&](UObject* script) {
            if (!is_uobject_valid_fast(script) || actor_owning_world(script) != world) { return false; }
            const auto type = script->GetClassPrivate()->GetPathName();
            if (type != enemy_type && type != camera_type) { return false; }
            if (!listeners.ContainsByPredicate([&](const auto& entry) { return entry.Object.Get() == script && entry.FunctionName == callback; }))
            {
                listeners.Add(FScriptDelegate{make_uobject_weak(script), callback});
                RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Graveyard coaster bound {} to {}\n"), script->GetName(), incoming->GetName());
            }
            return true;
        };
        if (outgoing != incoming && is_uobject_valid_fast(outgoing) && actor_owning_world(outgoing) == world &&
            outgoing->GetPropertyByNameInChain(L"OnPlayerSpecificAction") == field)
        {
            auto& old = field->ContainerPtrToValuePtr<FMulticastScriptDelegate>(outgoing)->InvocationList;
            for (int i = old.Num() - 1; i >= 0; --i)
            {
                if (old[i].FunctionName == callback && bind(old[i].Object.Get())) { old.RemoveAt(i); }
            }
        }
        for (const auto* package : {L"/Game/Levels/kg/kg_01/umap/kg_01_enemy_01", L"/Game/Levels/kg/kg_01/umap/kg_01_camera_ms011"})
        {
            Kh02ReflectedCall stream(L"/Script/Engine.Default__GameplayStatics", L"GetStreamingLevel", "graveyard_coaster", 128);
            stream.object(L"WorldContextObject", incoming);
            stream.name(L"PackageName", package);
            stream.run();
            bind(object(object(stream.result(), L"LoadedLevel"), L"LevelScriptActor"));
        }
    }

    void graveyard_portal_jump(UObject* portal)
    {
        const auto story = is_uobject_valid_fast(portal) ? try_read_object_name_property(portal, L"FinishedFlagName") : std::nullopt;
        graveyard_portal_pending = story && _wcsicmp(story->c_str(), L"gameflow_KG") == 0;
        if (graveyard_portal_pending)
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Graveyard Garden portal departure\n"));
    }

    void install_graveyard_events()
    {
        event_hooks = UObjectGlobals::RegisterHook(remote_event, [](UnrealScriptFunctionCallableContext& context, void*) {
            // The native thunk runs with the calling Blueprint's frame. Its locals
            // are not RemoteEvent parameters. Read only the literal (Self, Name)
            // operands used by this encounter; leave Code untouched for the game.
            const auto& frame = reinterpret_cast<const FFrame_50_AndBelow&>(context.TheStack);
            if (!frame.Code || !is_uobject_valid_fast(frame.Node)) { return; }
            const auto& code = frame.Node->GetScript();
            constexpr size_t operand_size = 15;
            const auto begin = reinterpret_cast<uintptr_t>(code.GetData());
            const auto pos = reinterpret_cast<uintptr_t>(frame.Code);
            if (code.Num() < operand_size || pos < begin || pos - begin > code.Num() - operand_size ||
                frame.Code[0] != 0x17 || frame.Code[1] != 0x21 || frame.Code[14] != 0x16) { return; }
            uint32_t script_name[3];
            std::memcpy(script_name, frame.Code + 2, sizeof(script_name));
            const uint32_t native_name[]{script_name[0], script_name[2]};
            FName event;
            static_assert(sizeof(event) == sizeof(native_name));
            std::memcpy(&event, native_name, sizeof(event));
            static const FName kill(L"kg_01_enemy_01_REvt_Gage", FNAME_Add), crowd(L"kg_01enemy_REvt_VFX_GageDown", FNAME_Add),
                retry_intro(L"kg_01_gameflow_REvt_Battle_01_RetryCamera_Start", FNAME_Add);
            if (event != kill && event != crowd && event != retry_intro) { return; }
            auto* script = frame.Object;
            if (!is_uobject_valid_fast(script) || script->GetClassPrivate() != frame.Node->GetOuterPrivate()) { return; }
            auto* world = actor_owning_world(script);
            if (!is_uobject_valid_fast(world) || world->GetPathName() != graveyard) { return; }
            if (event != retry_intro)
            {
                if (script->GetClassPrivate()->GetPathName() != enemy_type) { return; }
                auto* field = script->GetPropertyByNameInChain(event == kill ? L"kg_01_GageDown" : L"kg_01_VFXGageDown");
                if (!field || field->GetClass().GetName() != L"IntProperty" || field->GetElementSize() != sizeof(int32_t)) { return; }
                auto& points = *field->ContainerPtrToValuePtr<int32_t>(script);
                if (points > 0 && points <= std::numeric_limits<int32_t>::max() / 2)
                {
                    points *= 2;
                    auto& logged = score_logged[event == kill ? 0 : 1];
                    if (logged.Get() != script)
                    {
                        logged = make_uobject_weak(script);
                        RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Graveyard {} points {} -> {}\n"), event.ToString(), points / 2, points);
                    }
                }
                return;
            }
            if (!graveyard_portal_pending || script->GetClassPrivate()->GetPathName() != flow_type) { return; }
            auto* marker = object(script, L"TresPlayerStart_Retry01_ExecuteUbergraph_kg_01_gameflow_RefProperty");
            if (!marker || marker->GetOuterPrivate() != script->GetOuterPrivate()) { return; }
            Kh02ReflectedCall capture(script, L"CreateRetryPoint", "graveyard_retry", 128);
            capture.object(L"RetryPoint", marker);
            capture.boolean(L"WhiteFade", false);
            graveyard_portal_pending = false;
            capture.run();
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Graveyard portal checkpoint requested at {}\n"), marker->GetName());
        }, [](UnrealScriptFunctionCallableContext&, void*) {}, nullptr);
    }

    void uninstall_graveyard_events()
    {
        UObjectGlobals::UnregisterHook(remote_event, event_hooks);
        graveyard_portal_pending = false;
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
        const bool ship_command = special_ship_command(command);
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
            if (!situation && !ship_command) { return false; }
            break;
        }
        if (!pawn || character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn) ||
            !character_traversal::ready(pawn) || object(pawn, L"Controller") != controller ||
            object(object(controller, L"Player"), L"PlayerController") != controller ||
            actor_owning_world(pawn) != kh02_current_world())
        {
            return false;
        }
        if (ship_command)
        {
            // The alternate dispatchers omit all four ship specials. Sora's
            // branches use the shared pawn interface and the ship's normal
            // eligibility/activation path; these commands have no reactor.
            auto* state = object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
            auto* ship = object(state, L"m_pObj");
            auto* ship_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresVehiclePawnSpecialShip");
            return is_uobject_valid_fast(state) &&
                   state->GetClassPrivate()->GetPathName() == L"/Script/TresGame.TresPlayerState_SpecialShip" &&
                   object(state, L"MyCharPawn") == pawn &&
                   is_uobject_valid_fast(ship) && is_uobject_valid_fast(ship_class) &&
                   is_uobject_a_fast(ship, ship_class) &&
                   actor_owning_world(ship) == kh02_current_world() &&
                   object(ship, L"m_PlayerController") == controller;
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
        auto* world = actor_owning_world(pawn);
        if (!world || !is_uobject_valid_fast(actor) || actor_owning_world(actor) != world ||
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
