#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/grants/abilities/durable_ability_bank_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <format>
#include <string>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    namespace vbonus_detail
    {
        auto try_add_direct_durable_ability_source(const VanillaSoraAbilityBanks& banks,
                                                   uint64_t ability_value,
                                                   RuntimeState& runtime_state,
                                                   NativeVBonusHudPresenter* presenter,
                                                   bool& newly_granted,
                                                   std::wstring* failure_reason,
                                                   uint32_t auto_equip_mask) -> bool
        {
            newly_granted = false;
            const auto active_before =
                try_read_vanilla_ability_entry(banks.active_record, ability_value);
            const auto staging_before =
                try_read_vanilla_ability_entry(banks.staging_record, ability_value);
            if (!active_before.has_value() || !staging_before.has_value())
            {
                return fail_vanilla_ability_bank(
                    STR("direct_durable_ability_pre_grant_entry_unreadable"), failure_reason);
            }
            if (!runtime_durable_ability_banks_match(*active_before, *staging_before))
            {
                return fail_vanilla_ability_bank(
                    STR("direct_durable_ability_pre_grant_banks_out_of_sync"), failure_reason);
            }

            auto active_target = add_durable_ability_sources(*active_before, 1);
            auto staging_target = add_durable_ability_sources(*staging_before, 1);
            if (!active_target.has_value() || !staging_target.has_value())
            {
                // Both banks were readable and matched. A saturated ability
                // consumes this receipt without another copy, HUD, or retry.
                return true;
            }

            // Equip only the newly allocated source, preserving earlier manual
            // choices even while the camp menu's staging flags lag behind.
            const auto new_source = *active_target ^ *active_before;
            auto_equip_mask &= new_source >> 2;
            *active_target |= auto_equip_mask;
            *staging_target |= auto_equip_mask;

            // Resolve presentation before mutating a normal grant, but do not
            // require a HUD presenter for an exhausted-slot no-op.
            if (presenter &&
                !try_resolve_native_vbonus_hud_presenter(runtime_state, presenter, failure_reason))
            {
                return false;
            }

            const uint64_t active_address = banks.active_record + s_sora_ability_table_offset +
                                            ability_value * sizeof(uint32_t);
            const uint64_t staging_address = banks.staging_record + s_sora_ability_table_offset +
                                             ability_value * sizeof(uint32_t);
            const auto active_after = try_write_and_normalize_ability_entry(
                active_address, *active_target, banks.normalize_ability_entry);
            const auto staging_after = try_write_and_normalize_ability_entry(
                staging_address, *staging_target, banks.normalize_ability_entry);
            if (!active_after.has_value() || !staging_after.has_value() ||
                !runtime_durable_ability_banks_match(*active_after, *staging_after) ||
                (*active_after & runtime_ability_menu_flags) !=
                    ((*active_before & runtime_ability_menu_flags) | auto_equip_mask) ||
                (*staging_after & runtime_ability_menu_flags) !=
                    ((*staging_before & runtime_ability_menu_flags) | auto_equip_mask) ||
                vbonus_source_count(*active_after) != vbonus_source_count(*active_before) ||
                vbonus_source_count(*staging_after) != vbonus_source_count(*staging_before) ||
                durable_ability_source_count(*active_after) !=
                    durable_ability_source_count(*active_before) + 1 ||
                durable_ability_source_count(*staging_after) !=
                    durable_ability_source_count(*staging_before) + 1)
            {
                const bool active_rollback_succeeded = try_write_process_bytes(
                    active_address, &*active_before, sizeof(*active_before));
                const bool staging_rollback_succeeded = try_write_process_bytes(
                    staging_address, &*staging_before, sizeof(*staging_before));
                const bool rollback_succeeded =
                    active_rollback_succeeded && staging_rollback_succeeded;
                return fail_vanilla_ability_bank(
                    rollback_succeeded
                        ? std::format(
                              STR("direct_durable_ability_source_verification_failed:ability={}"),
                              ability_value)
                        : std::format(STR("direct_durable_ability_source_verification_and_rollback_"
                                          "failed:ability={}"),
                                      ability_value),
                    failure_reason);
            }

            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Direct durable Sora ability grant ability={} "
                    "active_before=0x{:X} durable_after=0x{:X} staging_after=0x{:X}\n"),
                ability_value, *active_before, *active_after, *staging_after);
            newly_granted = true;
            return true;
        }

        auto try_grant_unique_equipped_ability_source(const VanillaSoraAbilityBanks& banks,
                                                      uint64_t ability_value, bool* newly_granted,
                                                      RuntimeSoraAbilityEntryState* state,
                                                      std::wstring* failure_reason) -> bool
        {
            if (newly_granted)
            {
                *newly_granted = false;
            }
            if (state)
            {
                *state = RuntimeSoraAbilityEntryState{};
            }

            const auto active_before =
                try_read_vanilla_ability_entry(banks.active_record, ability_value);
            const auto staging_before =
                try_read_vanilla_ability_entry(banks.staging_record, ability_value);
            if (!active_before.has_value() || !staging_before.has_value())
            {
                return fail_vanilla_ability_bank(
                    STR("unique_equipped_ability_pre_grant_entry_unreadable"), failure_reason);
            }

            const bool active_owned = (*active_before & s_ability_owned_flag) != 0;
            const bool staging_owned = (*staging_before & s_ability_owned_flag) != 0;
            if (active_owned || staging_owned)
            {
                if (!active_owned || !staging_owned)
                {
                    return fail_vanilla_ability_bank(
                        std::format(
                            STR("unique_equipped_ability_ownership_banks_out_of_sync:ability={}"),
                            ability_value),
                        failure_reason);
                }
                if (state)
                {
                    state->owned = true;
                    state->equipped = (*active_before & s_ability_equipped_flag) != 0;
                }
                return true;
            }
            if (*active_before != *staging_before)
            {
                return fail_vanilla_ability_bank(
                    std::format(
                        STR("unique_equipped_ability_pre_grant_banks_out_of_sync:ability={}"),
                        ability_value),
                    failure_reason);
            }

            auto active_target = add_durable_ability_sources(*active_before, 1);
            auto staging_target = add_durable_ability_sources(*staging_before, 1);
            if (!active_target.has_value() || !staging_target.has_value())
            {
                return fail_vanilla_ability_bank(
                    std::format(STR("unique_equipped_ability_source_capacity_exhausted:ability={}"),
                                ability_value),
                    failure_reason);
            }
            *active_target |= s_ability_equipped_flag | s_ability_unseen_flag;
            *staging_target |= s_ability_equipped_flag | s_ability_unseen_flag;

            const uint64_t active_address = banks.active_record + s_sora_ability_table_offset +
                                            ability_value * sizeof(uint32_t);
            const uint64_t staging_address = banks.staging_record + s_sora_ability_table_offset +
                                             ability_value * sizeof(uint32_t);
            const auto active_after = try_write_and_normalize_ability_entry(
                active_address, *active_target, banks.normalize_ability_entry);
            const auto staging_after = try_write_and_normalize_ability_entry(
                staging_address, *staging_target, banks.normalize_ability_entry);
            const bool verified = active_after.has_value() && staging_after.has_value() &&
                                  *active_after == *staging_after &&
                                  (*active_after & s_ability_owned_flag) != 0 &&
                                  (*active_after & s_ability_equipped_flag) != 0 &&
                                  durable_ability_source_count(*active_after) ==
                                      durable_ability_source_count(*active_before) + 1;
            if (!verified)
            {
                const bool active_rollback_succeeded = try_write_process_bytes(
                    active_address, &*active_before, sizeof(*active_before));
                const bool staging_rollback_succeeded = try_write_process_bytes(
                    staging_address, &*staging_before, sizeof(*staging_before));
                return fail_vanilla_ability_bank(
                    active_rollback_succeeded && staging_rollback_succeeded
                        ? std::format(STR("unique_equipped_ability_verification_failed:ability={}"),
                                      ability_value)
                        : std::format(STR("unique_equipped_ability_verification_and_rollback_"
                                          "failed:ability={}"),
                                      ability_value),
                    failure_reason);
            }

            if (newly_granted)
            {
                *newly_granted = true;
            }
            if (state)
            {
                state->owned = true;
                state->equipped = true;
            }
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Unique equipped Sora ability grant ability={} "
                    "active_before=0x{:X} active_after=0x{:X} staging_after=0x{:X}\n"),
                ability_value, *active_before, *active_after, *staging_after);
            return true;
        }

        auto try_resolve_single_ability_value(const RuntimeItemGrantRequest& request,
                                              std::wstring* ability_token, uint64_t* ability_value,
                                              std::wstring* failure_reason) -> bool
        {
            if (!request.runtime_token.starts_with(STR("ETresAbilityKind::")) ||
                request.runtime_token.ends_with(STR("::NONE")))
            {
                if (failure_reason)
                {
                    *failure_reason = STR("durable_sora_ability_requires_primary_ability_token");
                }
                return false;
            }

            const auto resolved_value = try_resolve_runtime_enum_value(request.runtime_token);
            if (!resolved_value.has_value() || *resolved_value == 0 || *resolved_value > UINT8_MAX)
            {
                if (failure_reason)
                {
                    *failure_reason = std::format(STR("durable_sora_ability_enum_unresolved:{}"),
                                                  request.runtime_token);
                }
                return false;
            }
            if (ability_token)
            {
                *ability_token = request.runtime_token;
            }
            if (ability_value)
            {
                *ability_value = *resolved_value;
            }
            return true;
        }
    } // namespace vbonus_detail

    auto try_read_runtime_sora_ability_entry_state(uint8_t ability_value, UObject* sora_pawn,
                                                   RuntimeSoraAbilityEntryState* state,
                                                   std::wstring* failure_reason) -> bool
    {
        if (state)
        {
            *state = RuntimeSoraAbilityEntryState{};
        }
        if (!state)
        {
            return fail_vanilla_ability_bank(STR("runtime_sora_ability_state_result_missing"),
                                             failure_reason);
        }
        if (ability_value == 0 || !sora_pawn || !kh3ap::is_uobject_valid_fast(sora_pawn))
        {
            return fail_vanilla_ability_bank(STR("runtime_sora_ability_state_requires_sora_pawn"),
                                             failure_reason);
        }

        VanillaSoraAbilityBanks banks{};
        if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &banks, failure_reason))
        {
            return false;
        }
        const auto entry = try_read_vanilla_ability_entry(banks.active_record, ability_value);
        if (!entry.has_value())
        {
            return fail_vanilla_ability_bank(STR("runtime_sora_ability_state_entry_unreadable"),
                                             failure_reason);
        }
        state->owned = (*entry & s_ability_owned_flag) != 0;
        state->equipped = state->owned && (*entry & s_ability_equipped_flag) != 0;
        return true;
    }

    auto try_read_runtime_sora_ability_entry_states(uint8_t first_ability, UObject* sora_pawn,
                                                    std::span<RuntimeSoraAbilityEntryState> states,
                                                    std::wstring* failure_reason) -> bool
    {
        const auto result = read_runtime_sora_ability_snapshot(
            first_ability, states,
            [&](uint8_t first, std::span<uint32_t> entries)
            {
                if (!sora_pawn || !kh3ap::is_uobject_valid_fast(sora_pawn))
                {
                    return fail_vanilla_ability_bank(
                        STR("runtime_sora_ability_state_requires_sora_pawn"), failure_reason);
                }
                VanillaSoraAbilityBanks banks{};
                if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &banks, failure_reason))
                {
                    return false;
                }
                const uint64_t address =
                    banks.active_record + s_sora_ability_table_offset + first * sizeof(uint32_t);
                SIZE_T bytes_read = 0;
                if (!looks_like_process_pointer(banks.active_record) ||
                    !looks_like_process_pointer(address) ||
                    !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                       entries.data(), entries.size_bytes(), &bytes_read) ||
                    bytes_read != entries.size_bytes())
                {
                    return fail_vanilla_ability_bank(
                        STR("runtime_sora_ability_state_entry_unreadable"), failure_reason);
                }
                return true;
            });
        if (result == RuntimeSoraAbilitySnapshotResult::InvalidRange)
        {
            return fail_vanilla_ability_bank(STR("runtime_sora_ability_state_range_invalid"),
                                             failure_reason);
        }
        return result == RuntimeSoraAbilitySnapshotResult::Read;
    }

    auto try_read_runtime_sora_ability_entry_states(
        std::span<const uint8_t> abilities, UObject* sora_pawn,
        std::span<RuntimeSoraAbilityEntryState> states, std::wstring* failure_reason) -> bool
    {
        const auto result = read_runtime_sora_ability_selection(
            abilities, states,
            [&](uint8_t first, std::span<RuntimeSoraAbilityEntryState> range)
            {
                return try_read_runtime_sora_ability_entry_states(
                    first, sora_pawn, range, failure_reason);
            });
        if (result == RuntimeSoraAbilitySnapshotResult::InvalidRange)
        {
            return fail_vanilla_ability_bank(STR("runtime_sora_ability_selection_invalid"),
                                             failure_reason);
        }
        return result == RuntimeSoraAbilitySnapshotResult::Read;
    }

    auto try_grant_unique_equipped_runtime_sora_ability(uint8_t ability_value, UObject* sora_pawn,
                                                        bool* newly_granted,
                                                        RuntimeSoraAbilityEntryState* state,
                                                        std::wstring* failure_reason) -> bool
    {
        if (newly_granted)
        {
            *newly_granted = false;
        }
        if (state)
        {
            *state = RuntimeSoraAbilityEntryState{};
        }
        if (ability_value == 0 || !sora_pawn || !kh3ap::is_uobject_valid_fast(sora_pawn))
        {
            return fail_vanilla_ability_bank(
                STR("unique_equipped_runtime_sora_ability_requires_sora_pawn"), failure_reason);
        }

        VanillaSoraAbilityBanks banks{};
        if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &banks, failure_reason))
        {
            return false;
        }
        return try_grant_unique_equipped_ability_source(banks, ability_value, newly_granted, state,
                                                        failure_reason);
    }

    auto try_remove_runtime_sora_durable_ability_copy(uint8_t ability_value, UObject* sora_pawn,
                                                      std::wstring* failure_reason) -> bool
    {
        if (ability_value == 0 || !is_uobject_valid_fast(sora_pawn))
        {
            return fail_vanilla_ability_bank(STR("remove_ability_requires_sora_pawn"),
                                             failure_reason);
        }
        VanillaSoraAbilityBanks banks{};
        if (!try_resolve_vanilla_sora_ability_banks(sora_pawn, &banks, failure_reason))
        {
            return false;
        }
        const auto active = try_read_vanilla_ability_entry(banks.active_record, ability_value);
        const auto staging = try_read_vanilla_ability_entry(banks.staging_record, ability_value);
        if (!active || !staging || !runtime_durable_ability_banks_match(*active, *staging))
        {
            return fail_vanilla_ability_bank(STR("remove_ability_banks_unavailable_or_out_of_sync"),
                                             failure_reason);
        }
        const auto copies = durable_ability_source_count(*active);
        if (copies == 0)
        {
            return true;
        }
        uint32_t source{};
        for (const auto mask : s_ability_durable_source_masks)
        {
            if ((*active & mask) != 0)
            {
                source = mask;
            }
        }
        // Each direct source occupies a four-bit slot: owned/equipped/new/source.
        // Clear the removed slot, then let the native normalizer recompute counts.
        const auto slot_mask = source | (source >> 1) | (source >> 2) | (source >> 3);
        auto active_target = *active & ~slot_mask;
        auto staging_target = *staging & ~slot_mask;
        if (copies == 1 && vbonus_source_count(*active) == 0)
        {
            active_target &= ~runtime_ability_menu_flags;
            staging_target &= ~runtime_ability_menu_flags;
        }
        const auto active_address =
            banks.active_record + s_sora_ability_table_offset + ability_value * sizeof(uint32_t);
        const auto staging_address =
            banks.staging_record + s_sora_ability_table_offset + ability_value * sizeof(uint32_t);
        const auto active_after = try_write_and_normalize_ability_entry(
            active_address, active_target, banks.normalize_ability_entry);
        const auto staging_after = try_write_and_normalize_ability_entry(
            staging_address, staging_target, banks.normalize_ability_entry);
        if (!active_after || !staging_after ||
            !runtime_durable_ability_banks_match(*active_after, *staging_after) ||
            durable_ability_source_count(*active_after) + 1 != copies ||
            vbonus_source_count(*active_after) != vbonus_source_count(*active) ||
            ((copies == 1 && vbonus_source_count(*active) == 0) &&
             (*active_after & s_ability_owned_flag) != 0))
        {
            const bool active_rollback =
                try_write_process_bytes(active_address, &*active, sizeof(*active));
            const bool staging_rollback =
                try_write_process_bytes(staging_address, &*staging, sizeof(*staging));
            return fail_vanilla_ability_bank(
                active_rollback && staging_rollback
                    ? STR("remove_ability_verification_failed")
                    : STR("remove_ability_verification_and_rollback_failed"),
                failure_reason);
        }
        return true;
    }
} // namespace kh3ap
