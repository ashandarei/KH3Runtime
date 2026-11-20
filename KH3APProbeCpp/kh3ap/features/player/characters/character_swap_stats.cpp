#include "kh3ap/features/player/characters/character_swap_stats.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_bonus_policy.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/app/config.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/player/state/shared_sora_progression.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <cmath>
#include <cstring>
#include <stdexcept>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <DynamicOutput/DynamicOutput.hpp>
#endif
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_swap
{
    using namespace RC::Unreal;
    namespace
    {
        thread_local ReflectedPropertyCache<8> stat_properties;
        constexpr size_t base_level_offset = 0x40;
        constexpr size_t base_stats_offset = 0x50;
        constexpr int maximum_sora_level = 99;
        constexpr int maximum_stat_adjustment = 9999;
        constexpr int maximum_stat_sync_attempts = 3;

        constexpr int alternate_max_ap = 100;
        constexpr size_t base_strength_index = 3;
        constexpr size_t base_magic_index = 4;
        constexpr size_t base_ability_point_index = 6;
        constexpr uint8_t hp_break_kind = 18;
        constexpr size_t bad_status_kind_offset = 0x85d;
        constexpr size_t bad_status_time_offset = 0x860;
        constexpr size_t hp_break_cap_offset = 0x61c;
        constexpr size_t clear_bad_status_slot = 0xf38 / sizeof(void*);
        constexpr size_t apply_bad_status_slot = 0xf58 / sizeof(void*);

        template <typename T>
        auto pawn_field(UObject* pawn, size_t offset) -> T&
        {
            return *reinterpret_cast<T*>(reinterpret_cast<std::byte*>(pawn) + offset);
        }

        bool has_hp_break(UObject* pawn)
        {
            return pawn_field<uint8_t>(pawn, bad_status_kind_offset) == hp_break_kind &&
                   pawn_field<int32_t>(pawn, hp_break_cap_offset) > 0;
        }

        UObject* parameters(UObject* pawn)
        {
            auto* parameter = stat_properties.read_object(pawn, L"m_pBaseParam");
            if (character_native::build() == character_native::Build::Unsupported ||
                !is_uobject_valid_fast(parameter) ||
                parameter->GetClassPrivate()->GetName() != L"TresChrBaseParam")
            {
                throw std::runtime_error("character_stat_layout_unsupported");
            }
            return parameter;
        }

        int32_t read_stat(const Kh02DataTableView& table, void* row, const wchar_t* field)
        {
            auto* property = table.schema->GetPropertyByNameInChain(field);
            if (!row || !property || property->GetClass().GetName() != L"IntProperty" ||
                property->GetElementSize() != sizeof(int32_t) || property->GetArrayDim() != 1)
            {
                throw std::runtime_error("character_shared_stat_row_layout");
            }
            return *property->ContainerPtrToValuePtr<int32_t>(row);
        }

        std::array<int32_t, 4> level_stats(int level)
        {
            auto* asset = find_loaded_runtime_asset(
                L"/Game/DataTable/Player/LevelUp/p_ex001_LevelUpData.p_ex001_LevelUpData");
            const Kh02DataTableView table(asset, L"/Script/TresGame.TresChrLevelUpDataTable",
                                         "character_shared_level_table");
            auto* row = table.find(std::to_wstring(level).c_str());
            return {read_stat(table, row, L"m_AttackPower"), read_stat(table, row, L"m_MagicPower"),
                    read_stat(table, row, L"m_DefensePower"), read_stat(table, row, L"m_AbilityPoint")};
        }

        std::array<int32_t, 2> pandora_bonuses()
        {
            KeybladeLevelQueryResult level{};
            const bool maxed = seed_keyblade_levels_start_at_max(installed_seed_slot_data());
            if (!try_query_keyblade_level(L"Pandora's Power", maxed, &level, nullptr) ||
                level.current_level == 0)
            {
                return {};
            }
            auto* asset = find_loaded_runtime_asset(settings::s_tres_item_weapon_enhance_data_path);
            const Kh02DataTableView table(asset, L"/Script/TresGame.TresItemWeaponEnhanceDataTable",
                                         "character_pandora_level_table");
            std::array<int32_t, 2> bonuses{};
            for (int offset = 0; offset < level.current_level; ++offset)
            {
                const int flag = pandoras_power_first_enhancement_flag + offset;
                auto* row = table.find((L"IW_" + std::to_wstring(flag)).c_str());
                if (read_stat(table, row, L"m_FlagIndex") != flag)
                {
                    throw std::runtime_error("character_pandora_flag_mismatch");
                }
                bonuses[0] += read_stat(table, row, L"m_AttackPlus");
                bonuses[1] += read_stat(table, row, L"m_MagicPlus");
            }
            return bonuses;
        }

    } // namespace

    auto CharacterSwapHpBreak::capture(UObject* pawn) -> std::optional<CharacterSwapHpBreak>
    {
        using Clock = std::chrono::steady_clock;
        if (!is_uobject_valid_fast(pawn) ||
            !character_native::verify_sites({character_native::Site::hp_break_max_hp}) ||
            !has_hp_break(pawn))
        {
            return std::nullopt;
        }
        const auto remaining = pawn_field<float>(pawn, bad_status_time_offset);
        if (!std::isfinite(remaining) || remaining <= 0)
        {
            return std::nullopt;
        }
        return CharacterSwapHpBreak{
            pawn_field<int32_t>(pawn, hp_break_cap_offset), remaining, Clock::now()};
    }

    void CharacterSwapHpBreak::clear_parked(UObject* pawn)
    {
        if (!is_uobject_valid_fast(pawn) ||
            !character_native::verify_sites({character_native::Site::hp_break_max_hp}) ||
            !has_hp_break(pawn) ||
            !character_native::verify_sites({character_native::Site::clear_bad_status}))
        {
            return;
        }
        const auto clear = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                           character_native::rva(character_native::Site::clear_bad_status);
        const auto* vtable = *reinterpret_cast<const uintptr_t* const*>(pawn);
        if (vtable[clear_bad_status_slot] == clear)
        {
            reinterpret_cast<void (*)(UObject*, uint8_t)>(clear)(pawn, hp_break_kind);
        }
    }

    void CharacterSwapHpBreak::restore(UObject* pawn) const
    {
        const auto elapsed = std::chrono::duration<float>(
                                 std::chrono::steady_clock::now() - captured_at).count();
        const auto remaining = seconds - elapsed;
        if (remaining <= 0 || !is_uobject_valid_fast(pawn) ||
            !character_native::verify_sites({character_native::Site::apply_bad_status}))
        {
            return;
        }
        const auto apply = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                           character_native::rva(character_native::Site::apply_bad_status);
        const auto* vtable = *reinterpret_cast<const uintptr_t* const*>(pawn);
        if (vtable[apply_bad_status_slot] != apply)
        {
            return;
        }
        const auto kind = pawn_field<uint8_t>(pawn, bad_status_kind_offset);
        if (kind != 0 && kind != hp_break_kind)
        {
            return;
        }
        const auto owner = make_uobject_weak(pawn);
        using ApplyBadStatus = void (*)(UObject*, uint8_t, float, void*, void*);
        reinterpret_cast<ApplyBadStatus>(apply)(pawn, hp_break_kind, remaining, nullptr, nullptr);
        if (owner.Get() == pawn && pawn_field<uint8_t>(pawn, bad_status_kind_offset) == hp_break_kind)
        {
            // The native setter derives the cap from current HP. The outgoing
            // cap can be higher than current HP after a non-HP-Break hit.
            pawn_field<int32_t>(pawn, hp_break_cap_offset) = cap;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] HP Break carried across character swap pawn={} cap={} captured_seconds={} remaining_seconds={}\n"),
                pawn->GetName(), cap, seconds, pawn_field<float>(pawn, bad_status_time_offset));
#endif
        }
    }

    auto character_integer_value(UObject* pawn, const wchar_t* method) -> int
    {
        ReflectedFunctionCall call(pawn, method, "character_swap", 256);
        call.run();
        const auto result =
            read_function_int_param(call.function, call.parameters.data(), L"ReturnValue");
        if (!result)
        {
            throw std::runtime_error("character_integer_result_missing");
        }
        return static_cast<int>(*result);
    }

    auto CharacterSwapStats::totals(UObject* pawn) -> std::array<int32_t, 5>
    {
        auto* parameter = parameters(pawn);
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // Native getters include equipment, food and boosts on each storefront.
        const std::array<uintptr_t, 3> getters{
            character_native::rva(character_native::Site::stat_strength),
            character_native::rva(character_native::Site::stat_magic),
            character_native::rva(character_native::Site::stat_defense)};
        if (!character_native::verify_sites({character_native::Site::stat_strength,
                                             character_native::Site::stat_magic,
                                             character_native::Site::stat_defense}))
        {
            throw std::runtime_error("character_stat_getter_signature");
        }
        std::array<int32_t, 5> result{character_integer_value(pawn, L"GetMaxHitPoint"),
                                      character_integer_value(pawn, L"GetMaxMagicPoint")};
        if (character_native::verify_sites({character_native::Site::hp_break_max_hp}) &&
            has_hp_break(pawn) &&
            character_native::verify_sites({character_native::Site::stat_hp}))
        {
            // Stat synchronization must use the permanent maximum. The
            // temporary combat cap is restored separately after possession.
            result[0] = reinterpret_cast<int32_t (*)(UObject*)>(
                base + character_native::rva(character_native::Site::stat_hp))(parameter);
        }
        for (size_t index = 0; index < getters.size(); ++index)
        {
            result[index + 2] =
                reinterpret_cast<int32_t (*)(UObject*)>(base + getters[index])(parameter);
        }
        return result;
    }

    auto CharacterSwapStats::validate(UObject* pawn) -> void
    {
        static_cast<void>(parameters(pawn));
    }

    auto CharacterSwapStats::capture(UObject* pawn) -> bool
    {
        auto* parameter = parameters(pawn);
        std::memcpy(&sora_level, reinterpret_cast<std::byte*>(parameter) + base_level_offset,
                    sizeof(sora_level));
        std::memcpy(sora_stats.data(), reinterpret_cast<std::byte*>(parameter) + base_stats_offset,
                    sizeof(sora_stats));
        if (sora_level < 1 || sora_level > maximum_sora_level || sora_stats[0] <= 0 ||
            sora_stats[1] < 0)
        {
            return false;
        }
        sora_totals = totals(pawn);
        return true;
    }

    auto CharacterSwapStats::apply_base(UObject* pawn) const -> void
    {
        auto* parameter = parameters(pawn);
        std::memcpy(reinterpret_cast<std::byte*>(parameter) + base_level_offset, &sora_level,
                    sizeof(sora_level));
        std::memcpy(reinterpret_cast<std::byte*>(parameter) + base_stats_offset, sora_stats.data(),
                    sizeof(sora_stats));
        auto* base_stats =
            reinterpret_cast<int32_t*>(reinterpret_cast<std::byte*>(parameter) + base_stats_offset);
        base_stats[base_ability_point_index] = alternate_max_ap;
        synchronize_bonuses(pawn);
    }

    void CharacterSwapStats::apply_shared_progression(UObject* pawn)
    {
        // Keep the outgoing Sora base cache, including permanent bonuses, and
        // advance it by the seed's authored level-row difference.
        auto* parameter = parameters(pawn);
        const auto bonuses = pandora_bonuses();
        if (const auto progression = read_shared_sora_progression(pawn);
            progression && progression->level != sora_level)
        {
            const auto before = level_stats(sora_level);
            const auto after = level_stats(progression->level);
            for (size_t i = 0; i < before.size(); ++i)
            {
                sora_stats[base_strength_index + i] += after[i] - before[i];
            }
            sora_level = progression->level;
        }

        auto stats = sora_stats;
        stats[base_strength_index] += bonuses[0];
        stats[base_magic_index] += bonuses[1];
        pawn_field<int32_t>(parameter, base_level_offset) = sora_level;
        std::memcpy(reinterpret_cast<std::byte*>(parameter) + base_stats_offset, stats.data(),
                    sizeof(stats));
        synchronize_bonuses(pawn);
    }

    auto CharacterSwapStats::synchronize_totals(UObject* pawn) const -> void
    {
        auto* parameter = parameters(pawn);
        constexpr std::array<size_t, 5> fields{0, 1, 3, 4, 5};
        // Compensate for each alternate's own weapon/gear, without borrowing
        // Sora's equipment or retaining pointers into its retired pawn.
        for (int attempt = 0; attempt < maximum_stat_sync_attempts; ++attempt)
        {
            const auto actual = totals(pawn);
            if (actual == sora_totals)
            {
                break;
            }
            for (size_t index = 0; index < fields.size(); ++index)
            {
                auto* field = reinterpret_cast<int32_t*>(reinterpret_cast<std::byte*>(parameter) +
                                                         base_stats_offset) +
                              fields[index];
                const auto adjusted =
                    static_cast<int64_t>(*field) + sora_totals[index] - actual[index];
                if (adjusted < -maximum_stat_adjustment || adjusted > maximum_stat_adjustment)
                {
                    throw std::runtime_error("character_stat_adjustment_range");
                }
                *field = static_cast<int32_t>(adjusted);
            }
        }
        if (totals(pawn) != sora_totals)
        {
            throw std::runtime_error("character_stat_readback_mismatch");
        }
    }

    void CharacterSwapStats::synchronize_bonuses(UObject* pawn)
    {
        using namespace vbonus_detail;
        auto* parameter = parameters(pawn);
        const auto& banks = native_vbonus_bank_function_cache();
        const auto& amounts = native_vbonus_stat_function_cache();
        if (!banks.ready || !amounts.ready)
        {
            throw std::runtime_error("character_bonus_native_functions_unavailable");
        }
        auto* owner = static_cast<UObject*>(banks.get_save_owner());
        if (!is_uobject_valid_fast(owner) || owner->GetClassPrivate()->GetName() != L"TresGameInstance")
        {
            throw std::runtime_error("character_bonus_save_owner_unavailable");
        }
        auto* save = reinterpret_cast<uint8_t*>(owner) + s_active_save_data_offset;
        auto* sora = save + s_sora_character_record_offset;
        auto* target = pawn_field<uint8_t*>(parameter, s_sora_record_from_equipment_offset);
        const auto address = reinterpret_cast<uintptr_t>(target);
        const auto in_bank = [address](const uint8_t* first, size_t count)
        {
            const auto begin = reinterpret_cast<uintptr_t>(first);
            return address >= begin && address < begin + count * character_bonuses::record_size &&
                   (address - begin) % character_bonuses::record_size == 0;
        };
        if (target == sora || (!in_bank(sora, 16) && !in_bank(save + 0x9572ac, 14)))
        {
            throw std::runtime_error("character_bonus_record_identity_mismatch");
        }
        const auto difficulty = pawn_field<int32_t>(owner, s_difficulty_offset);
        std::array<int32_t, 2> totals{};
        for (const auto kind : character_bonuses::shared_kinds)
        {
            if (kind > s_last_mp_vbonus_kind) { continue; }
            int32_t hp = 0;
            int32_t mp = 0;
            amounts.resolve_stat_amounts(difficulty, kind, &hp, &mp);
            const auto count = sora[character_bonuses::bonus_offset + kind];
            totals[0] += hp * count;
            totals[1] += mp * count;
        }
        if (totals[0] < 0 || totals[0] > INT16_MAX || totals[1] < 0 || totals[1] > INT16_MAX)
        {
            throw std::runtime_error("character_bonus_stat_range");
        }
        character_bonuses::synchronize(
            std::span<uint8_t, character_bonuses::record_size>{target, character_bonuses::record_size},
            std::span<const uint8_t, character_bonuses::record_size>{sora, character_bonuses::record_size});
        // Native stat getters add these signed bonus shorts to the base cache.
        // Assign from authoritative counts so retries, equipment refreshes and
        // pooled-pawn reuse cannot stack bonuses or lose them on the next swap.
        pawn_field<int16_t>(parameter, 0x6c) = static_cast<int16_t>(totals[0]);
        pawn_field<int16_t>(parameter, 0x6e) = static_cast<int16_t>(totals[1]);
    }
} // namespace kh3ap::character_swap
