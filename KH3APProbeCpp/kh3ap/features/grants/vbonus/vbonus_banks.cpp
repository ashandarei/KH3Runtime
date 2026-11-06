#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    namespace vbonus_detail
    {
        auto fail_vanilla_ability_bank(std::wstring_view reason, std::wstring* failure_reason)
            -> bool
        {
            if (failure_reason)
            {
                *failure_reason = reason;
            }
            return false;
        }

        auto try_resolve_vanilla_sora_ability_banks(UObject* sora_pawn,
                                                    VanillaSoraAbilityBanks* banks,
                                                    std::wstring* failure_reason) -> bool
        {
            if (!banks)
            {
                return fail_vanilla_ability_bank(STR("vanilla_vbonus_bank_result_missing"),
                                                 failure_reason);
            }
            *banks = VanillaSoraAbilityBanks{};

            const RuntimeHotPathProfileScope bank_profile{L"abilities:resolve_banks"};
            const auto& function_cache = native_vbonus_bank_function_cache();
            if (!function_cache.ready)
            {
                return fail_vanilla_ability_bank(function_cache.failure_reason, failure_reason);
            }

            banks->module_base = function_cache.module_base;
            {
                const RuntimeHotPathProfileScope profile{L"abilities:save_owner"};
                banks->save_owner = reinterpret_cast<uint64_t>(function_cache.get_save_owner());
            }
            if (!looks_like_process_pointer(banks->save_owner))
            {
                return fail_vanilla_ability_bank(STR("vanilla_vbonus_save_owner_unavailable"),
                                                 failure_reason);
            }

            banks->active_record =
                banks->save_owner + s_active_save_data_offset + s_sora_character_record_offset;
            banks->staging_record =
                banks->save_owner + s_staging_save_data_offset + s_sora_character_record_offset;
            banks->add_vbonus_to_equipment = function_cache.add_vbonus_to_equipment;
            banks->add_vbonus = function_cache.add_vbonus;
            banks->normalize_ability_entry = function_cache.normalize_ability_entry;

            {
                const RuntimeHotPathProfileScope profile{L"abilities:context_kind"};
                banks->persistent_only =
                    (RuntimeSpaceService::is_space_active() &&
                     RuntimeSpaceService::item_delivery_context() == sora_pawn) ||
                    (sora_pawn && persistent_player_reward_context() == sora_pawn);
            }
            if (!banks->persistent_only)
            {
                const auto equipment = try_read_process_value<uint64_t>(
                    reinterpret_cast<uint64_t>(sora_pawn) + s_sora_equipment_from_pawn_offset);
                const auto pawn_record = equipment.has_value()
                                             ? try_read_process_value<uint64_t>(
                                                   *equipment + s_sora_record_from_equipment_offset)
                                             : std::nullopt;
                if (!equipment.has_value() || !looks_like_process_pointer(*equipment) ||
                    !pawn_record.has_value() || *pawn_record != banks->active_record)
                {
                    return fail_vanilla_ability_bank(
                        STR("vanilla_vbonus_active_sora_record_identity_mismatch"), failure_reason);
                }
                banks->equipment = *equipment;
                banks->active_vbonus_target = function_cache.active_vbonus_target_is_sora_pawn
                                                  ? reinterpret_cast<uint64_t>(sora_pawn)
                                                  : banks->equipment;
                if (function_cache.active_vbonus_target_is_sora_pawn)
                {
                    const auto vtable =
                        try_read_process_value<uint64_t>(reinterpret_cast<uint64_t>(sora_pawn));
                    const auto active_mutator =
                        vtable.has_value() ? try_read_process_value<uint64_t>(
                                                 *vtable + s_egs_add_vbonus_virtual_slot_offset)
                                           : std::nullopt;
                    if (!active_mutator.has_value() ||
                        *active_mutator != banks->module_base + s_egs_add_vbonus_to_sora_pawn_rva)
                    {
                        return fail_vanilla_ability_bank(
                            STR("vanilla_vbonus_egs_sora_mutator_identity_mismatch"),
                            failure_reason);
                    }
                }
            }
            const auto difficulty =
                try_read_process_value<int32_t>(banks->save_owner + s_difficulty_offset);
            if (!difficulty.has_value())
            {
                return fail_vanilla_ability_bank(STR("vanilla_vbonus_difficulty_unreadable"),
                                                 failure_reason);
            }
            banks->difficulty = *difficulty;

            // A small read at each table is enough to validate the records;
            // intentionally avoid copying or scanning either full save bank.
            if (!try_read_process_value<uint32_t>(banks->active_record +
                                                  s_sora_ability_table_offset)
                     .has_value() ||
                !try_read_process_value<uint32_t>(banks->staging_record +
                                                  s_sora_ability_table_offset)
                     .has_value())
            {
                return fail_vanilla_ability_bank(
                    STR("vanilla_vbonus_sora_ability_tables_unreadable"), failure_reason);
            }
            return true;
        }

        auto try_read_vanilla_ability_entry(uint64_t character_record, uint64_t ability_value)
            -> std::optional<uint32_t>
        {
            if (!looks_like_process_pointer(character_record) ||
                ability_value >= s_sora_ability_entry_count || ability_value > UINT8_MAX)
            {
                return std::nullopt;
            }
            return try_read_process_value<uint32_t>(character_record + s_sora_ability_table_offset +
                                                    ability_value * sizeof(uint32_t));
        }

        auto try_read_vanilla_vbonus_count(uint64_t character_record, uint64_t bonus_value)
            -> std::optional<uint8_t>
        {
            const RuntimeHotPathProfileScope profile{L"commands:bonus_count_read"};
            if (!looks_like_process_pointer(character_record) ||
                bonus_value < s_first_vbonus_kind || bonus_value > s_last_vbonus_kind)
            {
                return std::nullopt;
            }
            return try_read_process_value<uint8_t>(character_record +
                                                   s_sora_vbonus_count_table_offset + bonus_value);
        }

        auto vbonus_source_count(uint32_t entry) -> uint32_t
        {
            return (entry & s_ability_vbonus_count_mask) >> s_ability_vbonus_count_shift;
        }

        auto durable_ability_source_count(uint32_t entry) -> size_t
        {
            return static_cast<size_t>(std::count_if(s_ability_durable_source_masks.begin(),
                                                     s_ability_durable_source_masks.end(),
                                                     [entry](uint32_t mask)
                                                     {
                                                         return (entry & mask) != 0;
                                                     }));
        }

        auto add_durable_ability_sources(uint32_t entry, size_t copies) -> std::optional<uint32_t>
        {
            for (const uint32_t mask : s_ability_durable_source_masks)
            {
                if (copies == 0)
                {
                    break;
                }
                if ((entry & mask) != 0)
                {
                    continue;
                }
                entry |= mask;
                --copies;
            }
            if (copies != 0)
            {
                return std::nullopt;
            }
            return entry;
        }

        auto try_write_and_normalize_ability_entry(uint64_t entry_address, uint32_t value,
                                                   NormalizeAbilityEntry normalizer)
            -> std::optional<uint32_t>
        {
            if (!normalizer || !try_write_process_bytes(entry_address, &value, sizeof(value)))
            {
                return std::nullopt;
            }
            normalizer(reinterpret_cast<void*>(entry_address));
            return try_read_process_value<uint32_t>(entry_address);
        }
    } // namespace vbonus_detail

} // namespace kh3ap
