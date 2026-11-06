#include "kh3ap/features/player/controller_abilities/models.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/app/enums.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <string>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto inspect_persistent_character_reward(UObject* context, const std::wstring& token,
                                             int& active, int& staging, int& magic_command) -> bool
    {
        VanillaSoraAbilityBanks banks{};
        auto value = try_resolve_runtime_enum_value(token);
        if (!value || !try_resolve_vanilla_sora_ability_banks(context, &banks, nullptr))
        {
            return false;
        }
        if (token.starts_with(L"ETresAbilityKind::"))
        {
            auto a = try_read_vanilla_ability_entry(banks.active_record, *value);
            auto b = try_read_vanilla_ability_entry(banks.staging_record, *value);
            if (!a || !b)
            {
                return false;
            }
            active = static_cast<int>(durable_ability_source_count(*a));
            staging = static_cast<int>(durable_ability_source_count(*b));
        }
        else if (token.starts_with(L"ETresVictoryBonusKind::"))
        {
            auto a = try_read_vanilla_vbonus_count(banks.active_record, *value);
            auto b = try_read_vanilla_vbonus_count(banks.staging_record, *value);
            if (!a || !b)
            {
                return false;
            }
            active = *a;
            staging = *b;
            if (*value >= s_first_magic_vbonus_kind && *value <= s_last_magic_vbonus_kind)
            {
                NativeVBonusDisplayHelpers helpers{};
                if (!try_resolve_native_vbonus_display_helpers(banks.module_base, false, true,
                                                               &helpers, nullptr))
                {
                    return false;
                }
                magic_command = helpers.resolve_magic_command(
                    static_cast<uint8_t>(*value), reinterpret_cast<void*>(banks.active_record));
            }
        }
        else
        {
            return false;
        }
        return true;
    }

    auto runtime_magic_fixture(UObject* sora, std::array<uint32_t, 2>& values,
                               const std::array<uint32_t, 2>* expected,
                               const std::array<uint32_t, 2>* replacement) -> bool
    {
        if (!is_uobject_valid_fast(sora))
        {
            return false;
        }
        VanillaSoraAbilityBanks banks{};
        if (!try_resolve_vanilla_sora_ability_banks(sora, &banks, nullptr))
        {
            return false;
        }
        const std::array<uint64_t, 2> records{banks.active_record, banks.staging_record};
        for (size_t i = 0; i < records.size(); ++i)
        {
            auto entry = try_read_vanilla_ability_entry(
                records[i], controller_ability_detail::ability_id::magic);
            if (!entry)
            {
                return false;
            }
            values[i] = *entry;
        }
        if (!replacement)
        {
            return true;
        }
        if (!expected || values != *expected)
        {
            return false;
        }
        const auto before = values;
        bool success = true;
        for (size_t i = 0; i < records.size(); ++i)
        {
            auto readback = try_write_and_normalize_ability_entry(
                records[i] + s_sora_ability_table_offset +
                    controller_ability_detail::ability_id::magic * sizeof(uint32_t),
                (*replacement)[i], banks.normalize_ability_entry);
            if (!readback || *readback != (*replacement)[i])
            {
                success = false;
                break;
            }
        }
        if (!success)
        {
            for (size_t i = 0; i < records.size(); ++i)
            {
                try_write_and_normalize_ability_entry(
                    records[i] + s_sora_ability_table_offset +
                        controller_ability_detail::ability_id::magic * sizeof(uint32_t),
                    before[i], banks.normalize_ability_entry);
            }
            return false;
        }
        values = *replacement;
        return true;
    }
#endif
} // namespace kh3ap
