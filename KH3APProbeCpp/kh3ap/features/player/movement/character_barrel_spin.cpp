#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/Property/FBoolProperty.hpp>

namespace kh3ap::character_barrel_spin
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto barrel_class_path = L"/Game/Blueprints/Gimmick/ca/g_ca_acrobatbarrel/"
                                           L"g_ca_acrobatbarrel_01.g_ca_acrobatbarrel_01_C";
        constexpr auto motion_name = L"ODanceSpecialAct1";
        constexpr uint8_t perform_reactor_command = 15;
        constexpr float interaction_radius = 350.0f;

        UObject *object(UObject *owner, const wchar_t *field)
        {
            return is_uobject_valid_fast(owner)
                       ? try_read_reflected_object_property_value(owner, field)
                       : nullptr;
        }
    } // namespace

    bool allows_state(UObject *pawn)
    {
        // Resolve only when the owned native queue requests OneAction. The real
        // selected barrel must own this request; other one-action gimmicks are
        // not implicitly enabled by borrowing this state.
        auto *info = object(object(pawn, L"Controller"), L"m_pUICommandInfo");
        return selected_barrel(object(info, L"m_Actor"), object(info, L"m_Component"), pawn);
    }

    bool selected_barrel(UObject *actor, UObject *reactor, UObject *pawn)
    {
        if (!pawn || character_swap::current_hero() != pawn ||
            !character_swap::owns_swap_pawn(pawn) ||
            !character_traversal::barrel_spin_ready(pawn) || !is_uobject_valid_fast(actor) ||
            actor->GetClassPrivate()->GetPathName() != barrel_class_path ||
            object(actor, L"MyReactor") != reactor || !is_uobject_valid_fast(reactor) ||
            kh02_actor_world(actor) != kh02_actor_world(pawn))
        {
            return false;
        }
        auto *motion = actor->GetPropertyByNameInChain(L"Player_PerformMotion");
        auto *command = reactor->GetPropertyByNameInChain(L"m_Command");
        auto *active = reactor->GetPropertyByNameInChain(L"m_bIsGimmickActive");
        auto *hidden = actor->GetPropertyByNameInChain(L"bHidden");
        if (!motion || motion->GetClass().GetName() != L"NameProperty" ||
            motion->GetElementSize() != sizeof(FName) ||
            *motion->ContainerPtrToValuePtr<FName>(actor) != FName(motion_name, FNAME_Add) ||
            !command || command->GetElementSize() != sizeof(uint8_t) ||
            *command->ContainerPtrToValuePtr<uint8_t>(reactor) != perform_reactor_command ||
            !active || !active->IsA<FBoolProperty>() ||
            !static_cast<FBoolProperty *>(active)->GetPropertyValueInContainer(reactor) ||
            !hidden || !hidden->IsA<FBoolProperty>() ||
            static_cast<FBoolProperty *>(hidden)->GetPropertyValueInContainer(actor))
        {
            return false;
        }
        Kh02ReflectedCall position(actor, L"K2_GetActorLocation", "barrel_position", 16);
        position.run();
        Kh02ReflectedCall player_position(pawn, L"K2_GetActorLocation", "barrel_player_position",
                                          16);
        player_position.run();
        const auto a = position.result_vector();
        const auto b = player_position.result_vector();
        float distance{};
        for (size_t axis = 0; axis < a.size(); ++axis)
        {
            const float delta = a[axis] - b[axis];
            distance += delta * delta;
        }
        return distance <= interaction_radius * interaction_radius;
    }
} // namespace kh3ap::character_barrel_spin
