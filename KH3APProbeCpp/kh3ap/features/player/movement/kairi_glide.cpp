#include <polyhook2/Detour/x64Detour.hpp>
#include "kairi_glide.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_layout.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <memory>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::kairi_glide
{
    namespace
    {
        using UObject = RC::Unreal::UObject;
        using JumpCapability = bool (*)(void*);
        uint64_t original_jump_capability{};
        std::unique_ptr<PLH::x64Detour> jump_hook;
        constexpr size_t jump_phase_offset = 0x78;
        constexpr size_t jump_flags_offset = 0x114;
        constexpr uint32_t doubleflight_window = 8;

        bool jump_capability(void* state_interface)
        {
            auto* pawn = reinterpret_cast<UObject*>(reinterpret_cast<uintptr_t>(state_interface) -
                character_native::layout::player_state_interface_offset);
            const auto original = reinterpret_cast<JumpCapability>(original_jump_capability);
            if (!is_uobject_valid_fast(pawn) || !character_swap::owns_swap_pawn(pawn) ||
                pawn->GetClassPrivate()->GetName() != L"p_ex006_Pawn_C")
            {
                return original(state_interface);
            }
            auto* component = try_read_reflected_object_property_value(pawn, L"MyStateComp");
            auto* state = is_uobject_valid_fast(component)
                ? try_read_reflected_object_property_value(component, L"m_pCurrentState") : nullptr;
            if (!is_uobject_valid_fast(state) ||
                state->GetClassPrivate()->GetName() != L"TresPlayerState_Jump" ||
                try_read_reflected_object_property_value(state, L"MyCharPawn") != pawn)
            {
                return original(state_interface);
            }
            const auto phase = *reinterpret_cast<const int32_t*>(reinterpret_cast<std::byte*>(state) + jump_phase_offset);
            if (phase < 0 || phase > 2) { return original(state_interface); }

            // Kairi's authored launch delays this window until the apex.
            // Open only that gate for the native query; it still enforces
            // Doubleflight ownership, its spent flag, pending states and locks.
            struct WindowScope
            {
                RC::Unreal::FWeakObjectPtr state;
                uint32_t previous;
                ~WindowScope()
                {
                    if (auto* live = state.Get())
                    {
                        auto& flags = *reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(live) + jump_flags_offset);
                        flags = (flags & ~doubleflight_window) | previous;
                    }
                }
            } scope{make_uobject_weak(state), 0};
            auto& flags = *reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(state) + jump_flags_offset);
            scope.previous = flags & doubleflight_window;
            flags |= doubleflight_window;
            return original(state_interface);
        }

        bool prepare_jump_priority()
        {
            if (jump_hook) { return true; }
            using character_native::Site;
            if (!character_native::verify_sites({Site::jump_capability, Site::jump_doubleflight_window}))
            {
                return false;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto hook = std::make_unique<PLH::x64Detour>(
                base + character_native::rva(Site::jump_capability),
                reinterpret_cast<uintptr_t>(&jump_capability), &original_jump_capability);
            if (!hook->hook()) { return false; }
            jump_hook = std::move(hook);
            return true;
        }
    }

    bool refresh(RC::Unreal::UObject* pawn, std::wstring* failure)
    {
        using namespace vbonus_detail;
        if (!is_uobject_valid_fast(pawn) || !character_swap::owns_swap_pawn(pawn) ||
            pawn->GetClassPrivate()->GetName() != L"p_ex006_Pawn_C")
        {
            return true;
        }
        if (!prepare_jump_priority())
        {
            return fail_vanilla_ability_bank(L"kairi_jump_priority_unavailable", failure);
        }
        const auto glide = try_resolve_runtime_enum_value(L"ETresAbilityKind::GLIDE");
        VanillaSoraAbilityBanks sora;
        if (!glide || *glide > UINT8_MAX ||
            !try_resolve_vanilla_sora_ability_banks(pawn, &sora, failure) || !sora.persistent_only)
        {
            return false;
        }
        const auto source = try_read_vanilla_ability_entry(sora.active_record, *glide);
        if (!source) { return fail_vanilla_ability_bank(L"kairi_glide_source_unavailable", failure); }
        if (!(*source & s_ability_owned_flag)) { return true; }

        auto* parameter = try_read_reflected_object_property_value(pawn, L"m_pBaseParam");
        if (!is_uobject_valid_fast(parameter) || parameter->GetClassPrivate()->GetName() != L"TresChrBaseParam")
        {
            return fail_vanilla_ability_bank(L"kairi_glide_parameter_unavailable", failure);
        }
        const auto record = try_read_process_value<uint64_t>(
            reinterpret_cast<uint64_t>(parameter) + s_sora_record_from_equipment_offset);
        if (!record || *record == sora.active_record || *record == sora.staging_record)
        {
            return fail_vanilla_ability_bank(L"kairi_glide_requires_own_character_record", failure);
        }
        const auto before = try_read_vanilla_ability_entry(*record, *glide);
        if (!before) { return fail_vanilla_ability_bank(L"kairi_glide_entry_unavailable", failure); }
        // Ownership, not equipped state, makes this idempotent. An existing
        // copy may have been manually unequipped in Kairi's ability menu.
        if (*before & s_ability_owned_flag) { return true; }
        // BP_AbilityAdd does not retain this new movement ability in Kairi's
        // native bank. Use the existing durable-source mutator and the game's
        // verified normalizer, targeting only her own character record.
        const auto target = add_durable_ability_sources(*before | s_ability_equipped_flag, 1);
        if (!target)
        {
            return fail_vanilla_ability_bank(L"kairi_glide_source_slots_unavailable", failure);
        }
        const auto address = *record + s_sora_ability_table_offset + *glide * sizeof(uint32_t);
        const auto after = try_write_and_normalize_ability_entry(address, *target, sora.normalize_ability_entry);
        if (!after || (*after & (s_ability_owned_flag | s_ability_equipped_flag)) !=
                          (s_ability_owned_flag | s_ability_equipped_flag) ||
            durable_ability_source_count(*after) != durable_ability_source_count(*before) + 1)
        {
            const bool restored = try_write_process_bytes(address, &*before, sizeof(*before));
            return fail_vanilla_ability_bank(restored ? L"kairi_glide_grant_readback_failed"
                                                     : L"kairi_glide_grant_rollback_failed", failure);
        }
        return true;
    }
}
