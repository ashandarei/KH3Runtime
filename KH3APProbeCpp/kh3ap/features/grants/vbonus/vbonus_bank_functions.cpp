#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    namespace vbonus_detail
    {
        auto native_vbonus_module_cache() -> const NativeVBonusModuleCache&
        {
            static const NativeVBonusModuleCache cache = []()
            {
                NativeVBonusModuleCache result{};
                result.module = GetModuleHandleW(nullptr);
                result.profile = native_keyblade_enhancement_profile_for_module(result.module);
                if (!result.module || !result.profile)
                {
                    return result;
                }
                result.module_base = reinterpret_cast<uint64_t>(result.module);
                result.ready = true;
                return result;
            }();
            return cache;
        }

        auto resolve_egs_vbonus_save_owner() -> void*
        {
            const auto& module = native_vbonus_module_cache();
            if (!module.ready || module.profile->storefront != STR("egs"))
            {
                return nullptr;
            }

            // Unlike Steam's accessor, the EGS accessor returns the active
            // FTresSaveData at owner + 0x870. Convert it back to the common
            // owner base so the shared active and staging offsets stay valid.
            const auto get_active_save_data = reinterpret_cast<GetSaveOwner>(
                module.module_base + module.profile->save_accessor_rva);
            const uint64_t active_save_data = reinterpret_cast<uint64_t>(get_active_save_data());
            if (!looks_like_process_pointer(active_save_data) ||
                active_save_data < s_active_save_data_offset)
            {
                return nullptr;
            }
            const uint64_t save_owner = active_save_data - s_active_save_data_offset;
            return looks_like_process_pointer(save_owner) ? reinterpret_cast<void*>(save_owner)
                                                          : nullptr;
        }

        auto add_egs_vbonus_to_character_record(void* character_record, uint8_t bonus,
                                                uint8_t ability, bool mark_unseen) -> bool
        {
            const auto& module = native_vbonus_module_cache();
            const uint64_t record_address = reinterpret_cast<uint64_t>(character_record);
            if (!module.ready || module.profile->storefront != STR("egs") ||
                !looks_like_process_pointer(record_address))
            {
                return false;
            }

            // EGS LTCG inlines Steam's character-record wrapper. Reproduce
            // that wrapper with the exact two helpers used by the EGS Sora
            // grant method: update the ability/VBonus table, then reconcile
            // the record's derived slot state.
            const auto add_vbonus_to_ability_table = reinterpret_cast<AddVBonusToAbilityTable>(
                module.module_base + s_egs_add_vbonus_to_ability_table_rva);
            const auto refresh_vbonus_character_record =
                reinterpret_cast<RefreshVBonusCharacterRecord>(
                    module.module_base + s_egs_refresh_vbonus_character_record_rva);
            const bool applied = add_vbonus_to_ability_table(
                reinterpret_cast<void*>(record_address + s_sora_ability_table_offset), bonus,
                ability, mark_unseen);
            if (applied)
            {
                refresh_vbonus_character_record(character_record, bonus);
            }
            return applied;
        }

        auto native_vbonus_bank_function_cache() -> const NativeVBonusBankFunctionCache&
        {
            static const NativeVBonusBankFunctionCache cache = []()
            {
                NativeVBonusBankFunctionCache result{};
                const auto& module = native_vbonus_module_cache();
                if (!module.ready)
                {
                    result.failure_reason = STR("vanilla_vbonus_bank_requires_supported_image");
                    return result;
                }

                const uint64_t save_accessor =
                    module.module_base + module.profile->save_accessor_rva;
                const bool is_steam = module.profile->storefront == STR("steam");
                const bool is_egs = module.profile->storefront == STR("egs");
                if (!is_steam && !is_egs)
                {
                    result.failure_reason = STR("vanilla_vbonus_bank_storefront_unsupported");
                    return result;
                }
                const uint64_t add_vbonus_to_equipment_address =
                    is_steam ? module.module_base + s_steam_add_vbonus_to_equipment_rva
                             : module.module_base + s_egs_add_vbonus_to_sora_pawn_rva;
                const uint64_t add_vbonus_address =
                    is_steam ? module.module_base + s_steam_add_vbonus_to_character_record_rva : 0;
                const uint64_t normalize_ability_entry_address =
                    module.module_base + (is_steam ? s_steam_normalize_ability_entry_rva
                                                   : s_egs_normalize_ability_entry_rva);
                const NativeKeybladeEnhancementSignature add_vbonus_signature{
                    s_steam_add_vbonus_signature.data(),
                    s_steam_add_vbonus_signature.size(),
                };
                const NativeKeybladeEnhancementSignature add_vbonus_to_equipment_signature{
                    is_steam ? s_steam_add_vbonus_to_equipment_signature.data()
                             : s_egs_add_vbonus_to_sora_pawn_signature.data(),
                    is_steam ? s_steam_add_vbonus_to_equipment_signature.size()
                             : s_egs_add_vbonus_to_sora_pawn_signature.size(),
                };
                const uint64_t egs_add_vbonus_to_ability_table_address =
                    module.module_base + s_egs_add_vbonus_to_ability_table_rva;
                const NativeKeybladeEnhancementSignature egs_add_vbonus_to_ability_table_signature{
                    s_egs_add_vbonus_to_ability_table_signature.data(),
                    s_egs_add_vbonus_to_ability_table_signature.size(),
                };
                const uint64_t egs_refresh_vbonus_character_record_address =
                    module.module_base + s_egs_refresh_vbonus_character_record_rva;
                const NativeKeybladeEnhancementSignature
                    egs_refresh_vbonus_character_record_signature{
                        s_egs_refresh_vbonus_character_record_signature.data(),
                        s_egs_refresh_vbonus_character_record_signature.size(),
                    };
                const NativeKeybladeEnhancementSignature normalize_ability_entry_signature{
                    s_steam_normalize_ability_entry_signature.data(),
                    s_steam_normalize_ability_entry_signature.size(),
                };
                if (!native_signature_matches(save_accessor,
                                              module.profile->save_accessor_signature))
                {
                    result.failure_reason = STR("vanilla_vbonus_save_accessor_signature_mismatch");
                    return result;
                }
                if (is_egs && !native_signature_matches(
                                  module.module_base + s_egs_save_accessor_active_data_tail_rva,
                                  {
                                      s_egs_save_accessor_active_data_tail_signature.data(),
                                      s_egs_save_accessor_active_data_tail_signature.size(),
                                  }))
                {
                    result.failure_reason =
                        STR("vanilla_vbonus_egs_save_accessor_result_signature_mismatch");
                    return result;
                }
                if (!native_signature_matches(add_vbonus_to_equipment_address,
                                              add_vbonus_to_equipment_signature))
                {
                    result.failure_reason =
                        STR("vanilla_vbonus_equipment_mutator_signature_mismatch");
                    return result;
                }
                if (is_steam && !native_signature_matches(add_vbonus_address, add_vbonus_signature))
                {
                    result.failure_reason = STR("vanilla_vbonus_record_mutator_signature_mismatch");
                    return result;
                }
                if (is_egs && !native_signature_matches(egs_add_vbonus_to_ability_table_address,
                                                        egs_add_vbonus_to_ability_table_signature))
                {
                    result.failure_reason =
                        STR("vanilla_vbonus_egs_ability_table_mutator_signature_mismatch");
                    return result;
                }
                if (is_egs &&
                    !native_signature_matches(egs_refresh_vbonus_character_record_address,
                                              egs_refresh_vbonus_character_record_signature))
                {
                    result.failure_reason =
                        STR("vanilla_vbonus_egs_record_refresh_signature_mismatch");
                    return result;
                }
                if (!native_signature_matches(normalize_ability_entry_address,
                                              normalize_ability_entry_signature))
                {
                    result.failure_reason =
                        STR("vanilla_ability_entry_normalizer_signature_mismatch");
                    return result;
                }

                result.module_base = module.module_base;
                result.get_save_owner = is_steam ? reinterpret_cast<GetSaveOwner>(save_accessor)
                                                 : &resolve_egs_vbonus_save_owner;
                result.add_vbonus_to_equipment =
                    reinterpret_cast<AddVBonusToEquipment>(add_vbonus_to_equipment_address);
                result.add_vbonus =
                    is_steam ? reinterpret_cast<AddVBonusToCharacterRecord>(add_vbonus_address)
                             : &add_egs_vbonus_to_character_record;
                result.normalize_ability_entry =
                    reinterpret_cast<NormalizeAbilityEntry>(normalize_ability_entry_address);
                result.active_vbonus_target_is_sora_pawn = is_egs;
                result.ready = true;
                return result;
            }();
            return cache;
        }
    } // namespace vbonus_detail

} // namespace kh3ap
