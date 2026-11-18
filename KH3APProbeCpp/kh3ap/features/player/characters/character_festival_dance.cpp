#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/characters/character_festival_dance.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <memory>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_festival_dance
{
    using namespace RC::Unreal;
    namespace
    {
        using Update = void (*)(UObject*, float);
        using HideCommand = void (*)(UObject*, bool);
        uint64_t original_update{};
        std::unique_ptr<PLH::x64Detour> update_hook;
        uintptr_t hide_command{};

        UObject* object(UObject* owner, const wchar_t* name)
        {
            return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, name) : nullptr;
        }

        void update(UObject* state, float delta)
        {
            reinterpret_cast<Update>(original_update)(state, delta);
            auto* pawn = object(state, L"MyCharPawn");
            if (!pawn || !character_swap::owns_swap_pawn(pawn) || pawn != character_swap::current_hero() ||
                object(object(pawn, L"MyStateComp"), L"m_pCurrentState") != state)
            {
                return;
            }
            auto* manager = object(state, L"m_pDanceMng");
            auto* controller = object(pawn, L"Controller");
            if (!is_uobject_valid_fast(manager) || actor_owning_world(manager) != actor_owning_world(pawn) ||
                object(controller, L"Pawn") != pawn)
            {
                return;
            }

            // The dance state's native enter sets this to 1 and its exit restores
            // the previous value. The opening cinematic can subsequently restore
            // an alternate's forced-walk multiplier (140 / 680). Keep the active
            // state's own multiplier; native dance updates still own MaxWalkSpeed
            // and its music/phase-dependent scaling.
            auto* movement = object(pawn, L"MyMovement");
            auto* rate = is_uobject_valid_fast(movement) ? movement->GetPropertyByNameInChain(L"WalkSpeedRate") : nullptr;
            if (rate && rate->GetClass().GetName() == L"FloatProperty" && rate->GetElementSize() == sizeof(float))
            {
                *rate->ContainerPtrToValuePtr<float>(movement) = 1.0f;
            }

            // A cinematic/controller handoff can also reopen the ordinary command
            // panel over HudDance. Use its native hide transition, only when it
            // has actually been reopened. HudDance owns the prompts and gauge.
            auto* cockpit = object(object(controller, L"MyHUD"), L"TresCockpit");
            auto* command = object(cockpit, L"m_pHudCommand");
            auto* dance = object(cockpit, L"m_pHudDance");
            if (!is_uobject_valid_fast(cockpit) || cockpit->GetClassPrivate()->GetName() != L"TresCockpit" ||
                !is_uobject_valid_fast(command) || command->GetClassPrivate()->GetName() != L"TresUIP_HudCommand" ||
                !is_uobject_valid_fast(dance))
            {
                return;
            }
            const auto* cockpit_bytes = reinterpret_cast<const std::byte*>(cockpit);
            const auto* command_bytes = reinterpret_cast<const std::byte*>(command);
            if (*reinterpret_cast<const int32_t*>(cockpit_bytes + 0x1bc) == 2 &&
                *reinterpret_cast<const uint8_t*>(command_bytes + 0x9e9))
            {
                reinterpret_cast<HideCommand>(hide_command)(command, true);
            }
        }
    }

    bool initialize()
    {
        if (update_hook && original_update)
        {
            return true;
        }
        if (!character_native::verify_sites({character_native::Site::festival_dance_update,
                                            character_native::Site::hud_command_hide}))
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        hide_command = base + character_native::rva(character_native::Site::hud_command_hide);
        update_hook = std::make_unique<PLH::x64Detour>(
            base + character_native::rva(character_native::Site::festival_dance_update),
            reinterpret_cast<uintptr_t>(&update), &original_update);
        if (!update_hook->hook() || !original_update)
        {
            update_hook.reset();
            original_update = 0;
            return false;
        }
        return true;
    }
}
