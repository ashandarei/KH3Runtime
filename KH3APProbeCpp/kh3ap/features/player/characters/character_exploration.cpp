#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/characters/character_layout.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_exploration.h"
#include "kh3ap/features/player/movement/character_airstep.h"
#include "kh3ap/features/player/movement/character_floating.h"
#include "kh3ap/features/player/movement/character_rails.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/features/player/combat/character_underwater_combat.h"
#include "kh3ap/features/player/movement/alternate_wall_run.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <DynamicOutput/DynamicOutput.hpp>
#endif
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <array>
#include <memory>

namespace kh3ap::character_exploration
{
    using namespace RC::Unreal;
    namespace
    {
        const uintptr_t lookup_rva = character_native::rva(character_native::Site::state_lookup);
        const uintptr_t status_flags_rva = character_native::rva(character_native::Site::hud_flags);
        const uintptr_t status_dirty_rva = character_native::rva(character_native::Site::hud_dirty);
        constexpr uint32_t mp_gauge_mask = 1u << 10;
        constexpr uint32_t mp_gauge_dirty_mask = 1u << 9;
        using Lookup = UObject** (*)(UObject*, int);
        using StatusFlags = void (*)(UObject*);
        uint64_t original_lookup{}, original_status_flags{};
        std::unique_ptr<PLH::x64Detour> lookup_hook, status_flags_hook;
        FWeakObjectPtr owner, owner_controller, owner_queue, status;
        FWeakObjectPtr state_owner, state_queue;
        struct SharedState
        {
            int id;
            const wchar_t* path;
            FWeakObjectPtr type;
            bool needs_traversal{};
        };
        std::array shared_states{
            SharedState{player_state_ids::glide, L"/Script/TresGame.TresPlayerState_Glide", {}, true},
            SharedState{player_state_ids::floating, L"/Script/TresGame.TresPlayerState_Floating", {}, true},
            SharedState{player_state_ids::athletic_flow, L"/Script/TresGame.TresPlayerState_AthleticFlow", {}, true},
            SharedState{player_state_ids::rail_slide, L"/Script/TresGame.TresPlayerState_RailSlide", {}, true},
            SharedState{player_state_ids::rail_slide_mi, L"/Script/TresGame.TresPlayerState_RailSlideMI", {}, true},
            SharedState{character_underwater_combat::state_id, L"/Script/TresGame.TresPlayerState_ComboSwim", {}, true},
            SharedState{character_shotlock::state_id, L"/Script/TresGame.TresPlayerState_ShootFlow", {}},
            SharedState{alternate_wall_run::state_id, L"/Script/TresGame.TresPlayerState_WallRun", {}},
            SharedState{player_state_ids::gimmick_attach, L"/Script/TresGame.TresPlayerState_GimmickAttach", {}},
            SharedState{player_state_ids::check, L"/Script/TresGame.TresPlayerState_Check", {}},
            SharedState{player_state_ids::shop_menu, L"/Script/TresGame.TresPlayerState_ShopMenu", {}},
            SharedState{character_traversal::open_chest_state, L"/Script/TresGame.TresPlayerState_OpenTreasureBox", {}, true},
            SharedState{character_traversal::swim_state, L"/Script/TresGame.TresPlayerState_Swim", {}, true},
            SharedState{character_traversal::special_ship_state, L"/Script/TresGame.TresPlayerState_SpecialShip", {}, true},
            SharedState{character_traversal::hopping_state, L"/Script/TresGame.TresPlayerState_Hopping", {}, true},
            SharedState{character_traversal::pole_turn_state, L"/Script/TresGame.TresPlayerState_PoleTurn", {}, true},
            SharedState{character_traversal::hold_camera_state, L"/Script/TresGame.TresPlayerState_HoldCamera", {}, true},
            SharedState{character_traversal::save_menu_state, L"/Script/TresGame.TresPlayerState_SaveMenu", {}, true},
            SharedState{character_barrel_spin::one_action_state, L"/Script/TresGame.TresPlayerState_OneAction", {}, true}};
        bool mp_enabled{};

        UObject* object(UObject* value, const wchar_t* field)
        {
            return is_uobject_valid_fast(value) ? try_read_reflected_object_property_value(value, field) : nullptr;
        }

        bool owns_status(UObject* value)
        {
            auto* hero = owner.Get();
            auto* controller = owner_controller.Get();
            return hero && controller && character_swap::current_hero() == hero &&
                   object(controller, L"Pawn") == hero &&
                   object(hero, L"Controller") == controller &&
                   object(object(value, L"m_pUIManager"), L"m_pPlayerController") == controller;
        }

        UObject* resolve_status(UObject* controller)
        {
            auto* manager = object(object(kh02_current_viewport(), L"GameInstance"), L"m_UIManager");
            if (!controller || !manager || object(manager, L"m_pPlayerController") != controller)
            {
                return nullptr;
            }
            if (auto* cached = status.Get(); cached && object(cached, L"m_pUIManager") == manager)
            {
                return cached;
            }
            // The active manager owns its small UI-parts list. A cold HUD or
            // map change must not enumerate the entire game's UObject array.
            auto* field = manager->GetPropertyByNameInChain(L"m_UIPartsAry");
            if (!field || !field->IsA<FArrayProperty>())
            {
                return nullptr;
            }
            auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return nullptr;
            }
            const auto& parts = *field->ContainerPtrToValuePtr<TArray<UObject*>>(manager);
            constexpr int maximum_ui_parts = 256;
            if (parts.Num() < 0 || parts.Num() > maximum_ui_parts)
            {
                return nullptr;
            }
            for (auto* part : parts)
            {
                if (is_uobject_valid_fast(part) &&
                    part->GetClassPrivate()->GetName() == L"TresUIP_HudStatus" &&
                    object(part, L"m_pUIManager") == manager)
                {
                    status = make_uobject_weak(part);
                    return part;
                }
            }
            return nullptr;
        }

        void update_status_flags(UObject* value)
        {
            reinterpret_cast<StatusFlags>(original_status_flags)(value);
            if (!owns_status(value))
            {
                return;
            }
            status = make_uobject_weak(value);
            // The stock alternate HUD requests MP unconditionally when its
            // player presentation changes. Preserve that presentation and all
            // other flags, but use the same shared MP gate as Sora (Magic/Links).
            auto* flags = reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(value) + character_native::layout::hud_status_flags_offset);
            *flags = ((*flags & ~mp_gauge_mask) | (mp_enabled ? mp_gauge_mask : 0)) | mp_gauge_dirty_mask;
        }

        UObject** lookup_state(UObject* queue, int state_id)
        {
            auto* result = reinterpret_cast<Lookup>(original_lookup)(queue, state_id);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (state_id == character_traversal::hold_camera_state && queue == state_queue.Get())
            {
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Character camera state lookup authored={} prepared={}\n"),
                    result && *result ? (*result)->GetName() : L"",
                    character_traversal::ready(state_owner.Get()));
            }
            if (state_id == character_barrel_spin::one_action_state && queue == state_queue.Get())
            {
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Character barrel state lookup authored={} allowed={}\n"),
                    result && *result ? (*result)->GetName() : L"",
                    character_barrel_spin::allows_state(state_owner.Get()));
            }
            static FWeakObjectPtr observed_hopping_queue;
            if (state_id == character_traversal::hopping_state && queue == state_queue.Get() &&
                observed_hopping_queue.Get() != queue)
            {
                observed_hopping_queue = make_uobject_weak(queue);
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Character hopping lookup owner={} authored={} prepared={}\n"),
                    state_owner.Get() ? state_owner.Get()->GetName() : L"", result && *result ? (*result)->GetName() : L"",
                    character_traversal::ready(state_owner.Get()));
            }
#endif
            if (result || queue != state_queue.Get() ||
                !state_owner.Get() || !character_swap::owns_swap_pawn(state_owner.Get()) ||
                object(state_owner.Get(), L"MyStateComp") != queue)
            {
                return result;
            }
            // Alternates omit shared exploration states from their native sets.
            // Keep the original eligibility, reactor and launcher routes, then
            // let their normal queue instantiate Sora's shared implementation
            // only when the owned alternate has no authored entry.
            // The borrowed result is consumed synchronously by the factory.
            static UObject* borrowed_class{};
            for (const auto& state : shared_states)
            {
                if (state.id == state_id)
                {
                    if ((state_id == player_state_ids::rail_slide || state_id == player_state_ids::rail_slide_mi) &&
                        !character_traversal::rail_ready(state_owner.Get()))
                    {
                        return nullptr;
                    }
                    if (state_id == character_barrel_spin::one_action_state &&
                        !character_barrel_spin::allows_state(state_owner.Get()))
                    {
                        return nullptr;
                    }
                    if (state_id == alternate_wall_run::state_id && !alternate_wall_run::ready(state_owner.Get()))
                    {
                        return nullptr;
                    }
                    if (state.needs_traversal && !character_traversal::ready(state_owner.Get()))
                    {
                        return nullptr;
                    }
                    borrowed_class = state.type.Get();
                    return borrowed_class ? &borrowed_class : nullptr;
                }
            }
            return nullptr;
        }
    }

    bool prepare()
    {
        if (!character_airstep::initialize() || !character_floating::initialize() || !character_rails::initialize()) { return false; }
        if (lookup_hook && status_flags_hook)
        {
            return true;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!character_native::verify_sites({character_native::Site::state_lookup, character_native::Site::hud_flags, character_native::Site::hud_dirty}))
        {
            return false;
        }
        for (auto& state : shared_states)
        {
            auto* state_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, state.path);
            if (!is_uobject_valid_fast(state_class))
            {
                return false;
            }
            state.type = make_uobject_weak(state_class);
        }
        lookup_hook = std::make_unique<PLH::x64Detour>(base + lookup_rva,
            reinterpret_cast<uintptr_t>(&lookup_state), &original_lookup);
        status_flags_hook = std::make_unique<PLH::x64Detour>(base + status_flags_rva,
            reinterpret_cast<uintptr_t>(&update_status_flags), &original_status_flags);
        if (!lookup_hook->hook() || !original_lookup || !status_flags_hook->hook() || !original_status_flags)
        {
            status_flags_hook.reset();
            lookup_hook.reset();
            return false;
        }
        return true;
    }

    bool prepare_pawn(UObject* pawn)
    {
        if (!lookup_hook || !character_swap::owns_swap_pawn(pawn)) { return false; }
        auto* queue = object(pawn, L"MyStateComp");
        if (!is_uobject_valid_fast(queue)) { return false; }
        state_owner = make_uobject_weak(pawn);
        state_queue = make_uobject_weak(queue);
        character_airstep::prepare(pawn, object(pawn, L"Controller"));
        return true;
    }

    std::optional<bool> observe_shop_state_write(uintptr_t component, uintptr_t state_address)
    {
        if (!component || component != reinterpret_cast<uintptr_t>(state_queue.Get()))
        {
            return std::nullopt;
        }
        auto* pawn = state_owner.Get();
        auto* current = reinterpret_cast<UObject*>(state_address);
        if (!pawn || pawn != character_swap::current_hero() || !character_swap::owns_swap_pawn(pawn) ||
            !is_uobject_valid_fast(current) || object(current, L"MyCharPawn") != pawn)
        {
            return std::nullopt;
        }
        auto* field = current->GetPropertyByNameInChain(L"MyStateID");
        if (!field || field->GetElementSize() != sizeof(uint8_t))
        {
            return std::nullopt;
        }
        return *field->ContainerPtrToValuePtr<uint8_t>(current) == player_state_ids::shop_menu;
    }

    UObject* shop_context(UObject* completed_shop_pawn)
    {
        auto* pawn = state_owner.Get();
        auto* player_field = is_uobject_valid_fast(pawn) ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID") : nullptr;
        if (!player_field || player_field->GetElementSize() != sizeof(uint8_t))
        {
            return nullptr;
        }
        const auto player = static_cast<PlayerId>(*player_field->ContainerPtrToValuePtr<uint8_t>(pawn));
        if (player != PlayerId::Aqua && player != PlayerId::Kairi && player != PlayerId::Roxas)
        {
            return nullptr;
        }
        auto* queue = state_queue.Get();
        auto* current = object(queue, L"m_pCurrentState");
        const auto active = observe_shop_state_write(reinterpret_cast<uintptr_t>(queue),
                reinterpret_cast<uintptr_t>(current));
        if (!active.has_value())
        {
            return nullptr;
        }
        if (*active)
        {
            return pawn;
        }
        auto* state_field = current->GetPropertyByNameInChain(L"MyStateID");
        return pawn == completed_shop_pawn && character_traversal::ready(pawn) && state_field &&
                       state_field->GetElementSize() == sizeof(uint8_t) &&
                       *state_field->ContainerPtrToValuePtr<uint8_t>(current) == player_state_ids::idle
                   ? pawn : nullptr;
    }

    void refresh_portrait(UObject* pawn)
    {
        if (!lookup_hook || !status_flags_hook) { return; }
        auto* controller = object(pawn, L"Controller");
        if (!controller || object(controller, L"Pawn") != pawn) { return; }
        const auto invalidate = [&](UObject* candidate)
        {
            // Native HUD invalidation rereads the possessed pawn's portrait ID
            // and marks the movie dirty on its next ordinary update. Hiding or
            // showing the HUD alone leaves that cached ID unchanged.
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            reinterpret_cast<void (*)(UObject*, uint8_t)>(base + status_dirty_rva)(candidate, 1);
            status = make_uobject_weak(candidate);
        };
        if (auto* candidate = resolve_status(controller))
        {
            invalidate(candidate);
        }
    }

    void refresh_unlocks(UObject* hero, bool enabled)
    {
        if (!hero || character_swap::current_hero() != hero || !lookup_hook || !status_flags_hook)
        {
            return;
        }
        const bool changed_owner = owner.Get() != hero;
        const bool changed_mp = mp_enabled != enabled;
        owner = make_uobject_weak(hero);
        owner_controller = make_uobject_weak(object(hero, L"Controller"));
        owner_queue = make_uobject_weak(object(hero, L"MyStateComp"));
        mp_enabled = enabled;
        if (auto* current_status = resolve_status(owner_controller.Get()); current_status && owns_status(current_status))
        {
            // The UI manager retains its HUD through the player factory swap.
            // Validate its new controller ownership before reusing it; dropping
            // this live reference on every possession forces a full object scan.
            // Controller readiness retries also revisit unchanged unlocks.
            // Native status invalidation replays setFocusGaugeLock, so only
            // invalidate for a new owner or changed MP presentation.
            if (changed_owner || changed_mp)
            {
                update_status_flags(current_status);
            }
            return;
        }
        // If the HUD is still being created, its native status callback applies
        // the current owner's flags. No idle discovery or retry loop is needed.
    }
}
