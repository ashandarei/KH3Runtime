#include "movement_auto_equip_policy.h"
#include "sora_ability_layout.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <array>
#include <cstring>
#include <format>

namespace kh3ap::vbonus_detail
{
    namespace
    {
        constexpr uint64_t egs_total_ap_getter_rva = 0x7052a0;
        constexpr uint64_t steam_total_ap_getter_rva = 0x5808b0;
        constexpr uint64_t base_parameter_record_offset = 0x130;

        // Native total AP getter: base AP, AP Boosts, weapon and accessories.
        // Both supported executables contain this identical complete leaf.
        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 103> total_ap_signature{
            0x48,0x8b,0x81,0x30,0x01,0x00,0x00,0x8b,0x51,0x68,0x48,0x85,0xc0,0x74,0x09,
            0x0f,0xb6,0x80,0x83,0x09,0x00,0x00,0x03,0xd0,0x48,0x8b,0x81,0x40,0x01,0x00,
            0x00,0x48,0x85,0xc0,0x74,0x05,0x8b,0x40,0x20,0x03,0xd0,0x48,0x63,0x81,0x50,
            0x01,0x00,0x00,0x85,0xc0,0x7e,0x30,0x4c,0x8b,0xc0,0x48,0x83,0xf8,0x03,0x7e,
            0x27,0x4c,0x8b,0x89,0x48,0x01,0x00,0x00,0xb9,0x03,0x00,0x00,0x00,0x0f,0x1f,
            0x80,0x00,0x00,0x00,0x00,0x49,0x8b,0x04,0xc9,0x48,0x85,0xc0,0x74,0x03,0x03,0x50,0x20,0x48,0xff,
            0xc1,0x49,0x3b,0xc8,0x7c,0xec,0x8b,0xc2,0xc3};
        // clang-format on

        auto byte_field(Kh02DataTableView& table, const wchar_t* name) -> RC::Unreal::FProperty*
        {
            auto* property = table.schema->GetPropertyByNameInChain(name);
            if (!property || property->GetClass().GetName() != L"ByteProperty" ||
                property->GetElementSize() != 1 || property->GetArrayDim() != 1 ||
                property->GetOffset_Internal() < 0 ||
                property->GetOffset_Internal() >= table.schema->GetStructureSize())
            {
                throw std::runtime_error("movement_auto_equip_ability_table_schema");
            }
            return property;
        }
    }

    auto try_movement_auto_equip_mask(const VanillaSoraAbilityBanks& banks,
                                      RC::Unreal::UObject* sora_pawn, uint64_t ability_value,
                                      uint32_t* mask, std::wstring* failure_reason) -> bool
    {
        *mask = 0;
        // A substituted character borrows persistent Sora inventory but has
        // its own AP stats. Never use those stats to equip Sora's abilities.
        if (banks.persistent_only)
        {
            return true;
        }
        try
        {
            const auto& module = native_vbonus_module_cache();
            if (!module.ready)
            {
                return fail_vanilla_ability_bank(L"movement_auto_equip_unsupported_image", failure_reason);
            }
            const uint64_t getter = module.module_base + (module.profile->storefront == L"egs"
                                                              ? egs_total_ap_getter_rva
                                                              : steam_total_ap_getter_rva);
            if (!native_signature_matches(getter, {total_ap_signature.data(), total_ap_signature.size()}))
            {
                return fail_vanilla_ability_bank(L"movement_auto_equip_ap_getter_signature", failure_reason);
            }
            auto* parameter = try_read_reflected_object_property_value(sora_pawn, L"m_pBaseParam");
            if (!is_uobject_valid_fast(parameter) ||
                parameter->GetClassPrivate()->GetName() != L"TresChrBaseParam" ||
                try_read_process_value<uint64_t>(reinterpret_cast<uint64_t>(parameter) +
                                                 base_parameter_record_offset) !=
                    banks.active_record)
            {
                return fail_vanilla_ability_bank(L"movement_auto_equip_parameter_identity", failure_reason);
            }
            const int32_t total_ap = reinterpret_cast<int32_t (*)(RC::Unreal::UObject*)>(getter)(parameter);

            auto* asset = find_loaded_runtime_asset(L"/Game/Load/Tres/TresAbilityData.TresAbilityData");
            Kh02DataTableView table(asset, L"/Script/TresGame.TresAbilityDataTable",
                                    "movement_auto_equip_ability_table_unavailable");
            auto* cost_field = byte_field(table, L"m_UseAP");
            auto* max_field = byte_field(table, L"m_MaxEquip");
            const auto entries =
                try_read_process_bytes(banks.active_record + s_sora_ability_table_offset,
                                       sora_ability_layout::entry_count * sizeof(uint32_t));
            if (!entries)
            {
                return fail_vanilla_ability_bank(L"movement_auto_equip_entries_unreadable", failure_reason);
            }
            int32_t used_ap = 0;
            uint32_t target_entry = 0;
            uint8_t target_cost = 0;
            uint8_t target_max = 0;
            for (uint32_t value = 1; value < sora_ability_layout::entry_count; ++value)
            {
                uint32_t entry = 0;
                std::memcpy(&entry, entries->data() + value * sizeof(entry), sizeof(entry));
                const auto equipped = equipped_ability_copies(entry);
                if (equipped == 0 && value != ability_value)
                {
                    continue;
                }
                auto* row = table.find(std::format(L"A{:05}", value).c_str());
                if (!row)
                {
                    return fail_vanilla_ability_bank(L"movement_auto_equip_missing_cost_row", failure_reason);
                }
                const auto cost = *cost_field->ContainerPtrToValuePtr<uint8_t>(row);
                used_ap += equipped * cost;
                if (value == ability_value)
                {
                    target_entry = entry;
                    target_cost = cost;
                    target_max = *max_field->ContainerPtrToValuePtr<uint8_t>(row);
                }
            }
            *mask = movement_auto_equip_mask(target_entry, total_ap, used_ap, target_cost, target_max);
            return true;
        }
        catch (const std::exception&)
        {
            return fail_vanilla_ability_bank(L"movement_auto_equip_ability_table_unavailable", failure_reason);
        }
    }
}
