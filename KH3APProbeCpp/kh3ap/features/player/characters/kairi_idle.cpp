#include "kh3ap/features/player/characters/character_profile.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/characters/kairi_idle.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/performance_build.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/diagnostics/character_swap_trace.h"
#endif
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/TArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#define NOMINMAX
#include <Windows.h>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace kh3ap::kairi_idle
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto max_kairi_animation_parameter_bytes = 256;

        constexpr auto kairi_idle_animset_load_uuid = 0x4b48338a;
        constexpr auto kairi_idle_animset_load_timeout = std::chrono::seconds(3);
        constexpr auto kairi_idle_blend_in_seconds = 0.2f;
        constexpr auto kairi_weapon_visible_vtable_offset = 0x1330;
        constexpr auto kairi_idle_animation_loop_mask = 2u;
        constexpr size_t pawn_battle_mode_flags_offset = 0x630;
        constexpr size_t state_owner_pawn_offset = 0x68;

        constexpr auto set_path = player_animation_support::kairi_idle_animation_set_path;
        const uintptr_t resolve_rva = character_native::rva(character_native::Site::idle_resolve);
        const uintptr_t battle_rva = character_native::rva(character_native::Site::set_battle);
        const uintptr_t play_rva = character_native::rva(character_native::Site::idle_play);
        const uintptr_t dismiss_rva = character_native::rva(character_native::Site::dismiss_weapon);
        const uintptr_t weapon_rva = character_native::rva(character_native::Site::set_weapon);
        const uintptr_t idle_update_rva =
            character_native::rva(character_native::Site::idle_update);
        constexpr float native_idle_delay_seconds = 3.0f;
        constexpr float relaxed_blend_seconds = 0.35f;
        using Resolve = void* (*)(UObject*, FName);
        using IdleUpdate = void (*)(UObject*, float);
        using SetBattle = void (*)(UObject*, bool);
        using DismissWeapon = void (*)(UObject*, bool, float);
        using SetWeapon = void (*)(UObject*, bool, bool, float);
        using Play = void* (*)(UObject*, FName, FName, float, uint32_t, float, float, float);
        uint64_t original_resolve{}, original_battle{}, original_play{}, original_dismiss{},
            original_weapon{}, original_idle_update{};
        std::unique_ptr<PLH::x64Detour> resolve_hook, battle_hook, play_hook, dismiss_hook,
            weapon_hook, idle_update_hook;
        FWeakObjectPtr owner, instance, requested_owner;
        FName battle_idle, relaxed_idle;
        bool disabled{};
        bool relaxed{};
        float remaining_idle_seconds{};
        std::chrono::steady_clock::time_point load_deadline{};

        UObject* object(UObject* value, const wchar_t* field)
        {
            return is_uobject_valid_fast(value)
                       ? try_read_reflected_object_property_value(value, field)
                       : nullptr;
        }

        bool in_battle(UObject* pawn)
        {
            // Verified EGS IsBattleMode getter: (DWORD[pawn + 0x630] >> 1) & 1.
            // No reflected calls or discovery in the animation lookup path.
            uint32_t flags{};
            std::memcpy(&flags, reinterpret_cast<std::byte*>(pawn) + pawn_battle_mode_flags_offset,
                        sizeof(flags));
            return (flags & kairi_idle_animation_loop_mask) != 0;
        }

        void* resolve(UObject* anim, FName name)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"animation_resolve"};
#endif
            auto original = reinterpret_cast<Resolve>(original_resolve);
            const auto effective_name = effective_animation_name(anim, name);
            if (effective_name != name)
            {
                if (auto* entry = original(anim, effective_name))
                {
                    return entry;
                }
            }
            return original(anim, name);
        }

        void* play(UObject* anim, FName name, FName slot, float blend, uint32_t flags, float rate,
                   float scale_xy, float scale_z)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"animation_play"};
#endif
            if (name == battle_idle && anim == instance.Get())
            {
                auto* pawn = owner.Get();
                if (pawn && relaxed && !in_battle(pawn))
                {
                    // Native idle entry can request an immediate pose change.
                    // Blend from the current pose; leave outgoing attacks and
                    // movement requests at their native timing.
                    blend = std::max(blend, relaxed_blend_seconds);
                }
            }
            return reinterpret_cast<Play>(original_play)(anim, name, slot, blend, flags, rate,
                                                         scale_xy, scale_z);
        }

        void refresh_idle(UObject* pawn)
        {
            auto* state = object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (!is_uobject_valid_fast(state) ||
                state->GetClassPrivate()->GetName() != L"TresPlayerState_Idle")
            {
                return;
            }
            Kh02ReflectedCall play(pawn, L"BP_AnimPlayAnimset", "Kairi exploration idle",
                                   max_kairi_animation_parameter_bytes);
            play.name(L"InAnimName", L"IdleB");
            play.name(L"InSlotNodeName", L"FullBody");
            play.number(L"InBlendInTime", kairi_idle_blend_in_seconds);
            play.boolean(L"InLoop", true);
            play.number(L"InPlayRate", 1.0f);
            play.boolean(L"InRootTrans", false);
            play.number(L"InRootTransScaleXY", 1.0f);
            play.number(L"InRootTransScaleZ", 1.0f);
            play.run();
        }

        void set_weapon(UObject* pawn, bool visible, bool effect, float rate)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"weapon_visibility"};
#endif
            if (pawn == owner.Get() && instance.Get())
            {
                // Attacks and idle entry draw the weapon through this native
                // event. Keep the combat idle until the native dismissal event.
                relaxed = !visible && !in_battle(pawn);
                remaining_idle_seconds = visible ? native_idle_delay_seconds : 0.0f;
            }
            reinterpret_cast<SetWeapon>(original_weapon)(pawn, visible, effect, rate);
        }

        void dismiss_weapon(UObject* pawn, bool effect, float rate)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"weapon_dismiss"};
#endif
            if (pawn == owner.Get() && instance.Get() && !in_battle(pawn))
            {
                // TresPlayerState_Idle owns Sora's three-second countdown and
                // calls this event when it expires. Alternate weapons can be
                // excluded by the native weapon-type filter; use its ordinary
                // visibility setter directly for this prepared Kairi only.
                if (remaining_idle_seconds <= 0.0f)
                {
                    set_weapon(pawn, false, effect, rate);
                }
                return;
            }
            reinterpret_cast<DismissWeapon>(original_dismiss)(pawn, effect, rate);
        }

        void idle_update(UObject* state, float delta_seconds)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"idle_update"};
#endif
            reinterpret_cast<IdleUpdate>(original_idle_update)(state, delta_seconds);
            if (relaxed || remaining_idle_seconds <= 0.0f)
            {
                return;
            }
            auto* pawn = owner.Get();
            UObject* state_pawn{};
            std::memcpy(&state_pawn, reinterpret_cast<std::byte*>(state) + state_owner_pawn_offset,
                        sizeof(state_pawn));
            if (!pawn || state_pawn != pawn || !instance.Get())
            {
                return;
            }
            if (in_battle(pawn))
            {
                remaining_idle_seconds = native_idle_delay_seconds;
                return;
            }
            // Kairi can report no equipped weapon, bypassing Sora's native
            // dismissal callback. Count only her existing active idle updates,
            // using Sora's verified three-second delay, and stop at completion.
            remaining_idle_seconds =
                std::max(0.0f, remaining_idle_seconds - std::max(0.0f, delta_seconds));
            if (remaining_idle_seconds == 0.0f)
            {
                set_weapon(pawn, false, true, 1.0f);
                try
                {
                    refresh_idle(pawn);
                }
                catch (...)
                {
                }
            }
        }

        void set_battle(UObject* pawn, bool enabled)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"battle_mode"};
#endif
            const bool owned = pawn == owner.Get() && instance.Get();
            const bool was_battle = owned && in_battle(pawn);
            reinterpret_cast<SetBattle>(original_battle)(pawn, enabled);
            if (owned && enabled)
            {
                relaxed = false;
                remaining_idle_seconds = native_idle_delay_seconds;
            }
            if (owned && was_battle != in_battle(pawn))
            {
                // A battle-mode transition can happen while already standing.
                // Refresh only that idle; never interrupt movement or an attack.
                try
                {
                    refresh_idle(pawn);
                }
                catch (...)
                {
                }
            }
        }

        bool install()
        {
            if (resolve_hook && battle_hook && play_hook && dismiss_hook && weapon_hook &&
                idle_update_hook)
            {
                return true;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!character_native::verify_sites(
                    {character_native::Site::idle_resolve, character_native::Site::set_battle,
                     character_native::Site::idle_play, character_native::Site::dismiss_weapon,
                     character_native::Site::set_weapon, character_native::Site::idle_update}))
            {
                return false;
            }
            resolve_hook = std::make_unique<PLH::x64Detour>(
                base + resolve_rva, reinterpret_cast<uintptr_t>(&resolve), &original_resolve);
            battle_hook = std::make_unique<PLH::x64Detour>(
                base + battle_rva, reinterpret_cast<uintptr_t>(&set_battle), &original_battle);
            play_hook = std::make_unique<PLH::x64Detour>(
                base + play_rva, reinterpret_cast<uintptr_t>(&play), &original_play);
            dismiss_hook = std::make_unique<PLH::x64Detour>(
                base + dismiss_rva, reinterpret_cast<uintptr_t>(&dismiss_weapon),
                &original_dismiss);
            weapon_hook = std::make_unique<PLH::x64Detour>(
                base + weapon_rva, reinterpret_cast<uintptr_t>(&set_weapon), &original_weapon);
            idle_update_hook = std::make_unique<PLH::x64Detour>(
                base + idle_update_rva, reinterpret_cast<uintptr_t>(&idle_update),
                &original_idle_update);
            if (!resolve_hook->hook() || !original_resolve || !battle_hook->hook() ||
                !original_battle || !play_hook->hook() || !original_play || !dismiss_hook->hook() ||
                !original_dismiss || !weapon_hook->hook() || !original_weapon ||
                !idle_update_hook->hook() || !original_idle_update)
            {
                idle_update_hook.reset();
                weapon_hook.reset();
                dismiss_hook.reset();
                play_hook.reset();
                battle_hook.reset();
                resolve_hook.reset();
                return false;
            }
            return true;
        }
    } // namespace

    FName effective_animation_name(UObject* anim, FName name)
    {
        if (name == battle_idle && anim == instance.Get())
        {
            auto* pawn = owner.Get();
            if (pawn && relaxed && !in_battle(pawn))
            {
                return relaxed_idle;
            }
        }
        return name;
    }

    bool initialize()
    {
        return !disabled && install();
    }

    bool prepare(UObject* pawn)
    {
        if (disabled || !is_uobject_valid_fast(pawn))
        {
            return true;
        }
        auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != 1 ||
            *id->ContainerPtrToValuePtr<uint8_t>(pawn) != player_id_value(PlayerId::Kairi))
        {
            return true;
        }
        auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
        if (!is_uobject_valid_fast(anim))
        {
            return false;
        }
        if (owner.Get() == pawn && instance.Get() == anim)
        {
            return true;
        }
        auto* set = find_loaded_runtime_asset(set_path);
        if (!is_uobject_valid_fast(set))
        {
            if (requested_owner.Get() != pawn)
            {
                if (!request_runtime_asset_load(pawn, set_path, nullptr,
                                                kairi_idle_animset_load_uuid))
                {
                    return true;
                }
                requested_owner = make_uobject_weak(pawn);
                load_deadline = std::chrono::steady_clock::now() + kairi_idle_animset_load_timeout;
            }
            if (std::chrono::steady_clock::now() >= load_deadline)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Kairi idle load timed out; preserving native idle\n"));
                return true;
            }
            return false;
        }
        auto* sets_property = anim->GetPropertyByNameInChain(L"MyAnimSets");
        if (anim->GetClassPrivate()->GetName() != L"p_ex006_Anim_C" ||
            set->GetClassPrivate()->GetName() != L"TresAnimSet" || !sets_property ||
            sets_property->GetClass().GetName() != L"ArrayProperty" ||
            sets_property->GetElementSize() != sizeof(TArray<UObject*>) || !initialize())
        {
            disabled = true;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Kairi idle injection unavailable; preserving native idle\n"));
            return true;
        }
        battle_idle = FName(L"IdleB", FNAME_Add);
        relaxed_idle = FName(L"Idle_GM", FNAME_Add);
        auto* sets = sets_property->ContainerPtrToValuePtr<TArray<UObject*>>(anim);
        bool present = false;
        for (auto* existing : *sets)
        {
            present = present || existing == set;
        }
        if (!present)
        {
            sets->Add(set);
        }
        // MyAnimSets is a reflected strong reference. The installed animation
        // stays alive with this ABP; no global asset or NPC table is modified.
        if (!reinterpret_cast<Resolve>(original_resolve)(anim, relaxed_idle))
        {
            throw std::runtime_error("kairi_idle_animation_missing");
        }
        owner = make_uobject_weak(pawn);
        instance = make_uobject_weak(anim);
        // Preparation can finish after native idle entry has already hidden
        // the weapon, so seed the state from the native getter once.
        const auto vtable = *reinterpret_cast<uintptr_t**>(pawn);
        const auto weapon_visible = reinterpret_cast<bool (*)(UObject*)>(
            vtable[kairi_weapon_visible_vtable_offset / sizeof(uintptr_t)]);
        relaxed = !in_battle(pawn) && !weapon_visible(pawn);
        remaining_idle_seconds = relaxed ? 0.0f : native_idle_delay_seconds;
        refresh_idle(pawn);
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Kairi ABP exploration idle ready: Idle_GM\n"));
        return true;
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool diagnostic_battle(bool enabled)
    {
        auto* pawn = owner.Get();
        auto* controller = object(pawn, L"Controller");
        if (!pawn || !instance.Get() || object(controller, L"Pawn") != pawn ||
            kh02_actor_world(pawn) != kh02_current_world())
        {
            return false;
        }
        Kh02ReflectedCall change(pawn, L"SetBattleMode", "Kairi idle battle validation",
                                 max_kairi_animation_parameter_bytes);
        change.boolean(L"InEnable", enabled);
        change.run();
        return in_battle(pawn) == enabled;
    }
#endif
} // namespace kh3ap::kairi_idle
