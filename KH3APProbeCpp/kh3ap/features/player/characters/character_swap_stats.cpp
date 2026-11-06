#include "kh3ap/features/player/characters/character_swap_stats.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <cstring>
#include <stdexcept>
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
        constexpr size_t base_ability_point_index = 6;
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

    } // namespace

    auto character_integer_value(UObject* pawn, const wchar_t* method) -> int
    {
        Kh02ReflectedCall call(pawn, method, "character_swap", 256);
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

    auto CharacterSwapStats::add_bonus(UObject* pawn, int hp_amount, int mp_amount) -> void
    {
        auto* parameter = parameters(pawn);
        auto* stats =
            reinterpret_cast<int32_t*>(reinterpret_cast<std::byte*>(parameter) + base_stats_offset);
        stats[0] += hp_amount;
        stats[1] += mp_amount;
        sora_stats[0] += hp_amount;
        sora_stats[1] += mp_amount;
        sora_totals[0] += hp_amount;
        sora_totals[1] += mp_amount;
    }
} // namespace kh3ap::character_swap
