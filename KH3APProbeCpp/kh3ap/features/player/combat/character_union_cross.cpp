#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/combat/character_union_cross.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/character_magic.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <array>
#include <memory>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_union_cross
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr size_t controller_selected_command_offset = 0x87c;
        constexpr int first_union_cross_finish_command = 180;
        constexpr int last_union_cross_finish_command = 185;
        constexpr size_t attached_state_owner_offset = 0x68;
        constexpr size_t attached_state_kind_offset = 0xa0;
        constexpr int union_cross_attach_kind = 6;
        constexpr size_t attached_ride_weak_pointer_offset = 0xa4;

        using ActionCommand = bool (*)(UObject*);
        std::array<uint64_t, 3> originals{};
        std::array<std::unique_ptr<PLH::x64Detour>, 3> hooks;
        ActionCommand sora_action_command{};
        bool attempted{};

        UObject* object(UObject* owner, const wchar_t* field)
        {
            return is_uobject_valid_fast(owner)
                ? try_read_reflected_object_property_value(owner, field) : nullptr;
        }

        template<typename T>
        T& field(UObject* value, size_t offset)
        {
            return *reinterpret_cast<T*>(reinterpret_cast<std::byte*>(value) + offset);
        }

        bool owns_union_cross_command(UObject* controller)
        {
            if (!is_uobject_valid_fast(controller)) { return false; }
            // Audited native controller dispatchers read this same selection.
            const int command = field<int>(controller, controller_selected_command_offset);
            if (command < first_union_cross_finish_command ||
                command > last_union_cross_finish_command)
            {
                return false;
            }
            auto* pawn = object(controller, L"Pawn");
            auto* local_player = object(controller, L"Player");
            if (!pawn || character_swap::current_hero() != pawn ||
                !character_swap::owns_swap_pawn(pawn) ||
                object(pawn, L"Controller") != controller ||
                object(local_player, L"PlayerController") != controller)
            {
                return false;
            }
            auto* state = object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (!is_uobject_valid_fast(state) ||
                state->GetClassPrivate()->GetPathName() !=
                    L"/Script/TresGame.TresPlayerState_GimmickAttach" ||
                field<UObject*>(state, attached_state_owner_offset) != pawn ||
                field<int>(state, attached_state_kind_offset) != union_cross_attach_kind)
            {
                return false;
            }
            // StartUnionCross stores its ride as a weak reference in this state.
            // Resolve it only for a requested Union Cross command, never on idle ticks.
            auto* ride = field<FWeakObjectPtr>(state, attached_ride_weak_pointer_offset).Get();
            if (!is_uobject_valid_fast(ride) || object(ride, L"PlayerPawn") != pawn ||
                ride->GetClassPrivate()->GetName() != L"g_kg_unioncross_ride_C")
            {
                return false;
            }
            auto* started = ride->GetPropertyByNameInChain(L"Started");
            return started && started->IsA<FBoolProperty>() &&
                static_cast<FBoolProperty*>(started)->GetPropertyValueInContainer(ride);
        }

        template<size_t Index>
        bool action_command(UObject* controller)
        {
            if (is_uobject_valid_fast(controller) && character_magic::owns_command(
                    controller, field<int>(controller, controller_selected_command_offset)))
            {
                // Alternate switches omit some ordinary spell families (Aqua
                // omits Water). Reuse Sora's normal selection/MP/targeting path
                // only for the prepared ally's mirrored unlocked commands.
                return sora_action_command(controller);
            }
            if (owns_union_cross_command(controller))
            {
                // Alternates omit FINISH_UNIONX / FINISH_UX1..5 from their
                // switch. Sora's exact branches use the shared pawn interface,
                // including native eligibility, ride animation and UI cleanup.
                // No command is synthesized and the ride's progress is untouched.
                const bool accepted = sora_action_command(controller);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Character Union Cross command controller={} accepted={}\n"),
                    controller->GetName(), accepted);
#endif
                return accepted;
            }
            return reinterpret_cast<ActionCommand>(originals[Index])(controller);
        }
    }

    void initialize()
    {
        if (attempted) { return; }
        attempted = true;
        using character_native::Site;
        if (!character_native::verify_sites({Site::union_action_sora, Site::union_action_kairi,
                Site::union_action_aqua, Site::union_action_roxas}))
        {
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Character Union Cross command support unavailable for this build\n"));
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        sora_action_command = reinterpret_cast<ActionCommand>(base + character_native::rva(Site::union_action_sora));
        const std::array sites{Site::union_action_kairi, Site::union_action_aqua, Site::union_action_roxas};
        const std::array<ActionCommand, 3> callbacks{action_command<0>, action_command<1>, action_command<2>};
        for (size_t index = 0; index < hooks.size(); ++index)
        {
            hooks[index] = std::make_unique<PLH::x64Detour>(base + character_native::rva(sites[index]),
                reinterpret_cast<uintptr_t>(callbacks[index]), &originals[index]);
            if (!hooks[index]->hook() || !originals[index])
            {
                for (auto& hook : hooks) { hook.reset(); }
                RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] Character Union Cross command hooks failed\n"));
                return;
            }
        }
        RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Character Union Cross command hooks installed\n"));
    }
}
