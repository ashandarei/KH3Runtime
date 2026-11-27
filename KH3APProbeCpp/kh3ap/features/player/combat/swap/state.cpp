#include "swap_state.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include <Unreal/Property/FBoolProperty.hpp>

namespace kh3ap::combat_swap_detail
{
    using namespace RC::Unreal;

    const uintptr_t queue_clear_rva = character_native::rva(character_native::Site::queue_clear);
    const uintptr_t queue_push_rva = character_native::rva(character_native::Site::queue_push);
    const uintptr_t queue_update_rva = character_native::rva(character_native::Site::queue_update);

    // Metadata survives pawn replacements, while every value is read from
    // the live receiver. Weak class serials invalidate unloaded layouts.
    thread_local ReflectedPropertyCache<128> movement_properties;

    int attack_kind(int player)
    {
        if (is_standard_sora_player_id(player))
        {
            return player_state_ids::attack;
        }
        if (player == player_id_value(PlayerId::Aqua))
        {
            return player_state_ids::attack_aqua;
        }
        if (player == player_id_value(PlayerId::Riku) ||
            player == player_id_value(PlayerId::RikuKH29))
        {
            return player_state_ids::attack_riku;
        }
        if (player == player_id_value(PlayerId::Roxas))
        {
            return player_state_ids::attack_roxas;
        }
        if (player == player_id_value(PlayerId::Kairi))
        {
            return player_state_ids::attack_kairi;
        }
        return -1;
    }

    auto object(UObject* owner, const wchar_t* name) -> UObject*
    {
        return movement_properties.read_object(owner, name);
    }

    auto player_id(UObject* pawn) -> int
    {
        auto* property = movement_properties.resolve(pawn, L"m_PlayerUniqueID");
        return property && property->GetElementSize() == 1
                   ? *property->ContainerPtrToValuePtr<uint8_t>(pawn)
                   : -1;
    }

    auto state(UObject* pawn) -> UObject*
    {
        return object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
    }

    auto state_id(UObject* current) -> int
    {
        auto* property = movement_properties.resolve(current, L"MyStateID");
        return property && property->GetElementSize() == 1
                   ? *property->ContainerPtrToValuePtr<uint8_t>(current)
                   : -1;
    }

    bool attack_state(UObject* current, UObject* pawn)
    {
        if (!is_uobject_valid_fast(current) || object(current, L"MyCharPawn") != pawn)
        {
            return false;
        }
        const auto name = current->GetClassPrivate()->GetName();
        auto* target = current->GetPropertyByNameInChain(L"m_pTargetPawn");
        return (name == L"TresPlayerState_Attack" || name == L"TresPlayerState_AttackAQ" ||
                name == L"TresPlayerState_AttackRI" || name == L"TresPlayerState_AttackKA" ||
                name == L"TresPlayerState_AttackRO") &&
               target && target->GetOffset_Internal() == 0x110;
    }

    auto vector(UObject* owner, const wchar_t* method) -> std::array<float, 3>
    {
        Call call(owner, method);
        call.run();
        return call.result_vector();
    }

    bool state_boolean(UObject* owner, const wchar_t* name)
    {
        auto* property = owner->GetPropertyByNameInChain(name);
        if (!property || !property->IsA<FBoolProperty>())
        {
            throw std::runtime_error("kh02_swing_boolean_layout");
        }
        return static_cast<FBoolProperty*>(property)->GetPropertyValue(
            property->ContainerPtrToValuePtr<void>(owner));
    }

    auto animation_value(UObject* anim, const wchar_t* method) -> float
    {
        Call call(anim, method);
        call.name(L"InSlotName", L"FullBody");
        call.run();
        auto* returned = call.param(L"ReturnValue");
        if (!returned || returned->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("kh02_swap_animation_value");
        }
        return *returned->ContainerPtrToValuePtr<float>(call.parameters.data());
    }

    auto local_player_pawn(UObject* world) -> UObject*
    {
        Call controller(L"/Script/Engine.Default__GameplayStatics", L"GetPlayerController");
        controller.object(L"WorldContextObject", world);
        controller.integer(L"PlayerIndex", 0);
        controller.run();
        auto* owner = controller.result();
        auto* pawn = object(owner, L"Pawn");
        return object(pawn, L"Controller") == owner ? pawn : nullptr;
    }

    auto movement_stick(UObject* pawn) -> std::array<float, 2>
    {
        return runtime_controller_stick_state(object(pawn, L"Controller"));
    }
} // namespace kh3ap::combat_swap_detail
