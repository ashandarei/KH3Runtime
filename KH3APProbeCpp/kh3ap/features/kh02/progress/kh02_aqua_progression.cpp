#include "kh02_aqua_progression.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/kh02/runtime/kh02_entry_assets.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/core/events/character_level_events.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UClass.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto curve_path =
            L"/Game/Zlueprints/Player/p_ex002/DataTable/p_ex002_LevelUpData.p_ex002_LevelUpData";

        template <class T> auto field(void* object, size_t offset) -> T&
        {
            return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
        }

        auto base_parameters(UObject* pawn) -> UObject*
        {
            auto* parameters = try_read_reflected_object_property_value(pawn, L"m_pBaseParam");
            if (character_native::build() == character_native::Build::Unsupported || !is_uobject_valid_fast(pawn) ||
                pawn->GetClassPrivate()->GetName() != L"p_ex002_Pawn_C" || !is_uobject_valid_fast(parameters) ||
                parameters->GetClassPrivate()->GetName() != L"TresChrBaseParam" ||
                parameters->GetOuterPrivate() != pawn)
            {
                throw std::runtime_error("kh02_aqua_level_parameter_layout");
            }
            return parameters;
        }

        auto modified_award(UObject* pawn, void* owner, int amount) -> int
        {
            if (character_native::build() == character_native::Build::Unsupported || !is_uobject_valid_fast(pawn))
            {
                throw std::runtime_error("kh02_shared_experience_build");
            }
            const bool steam = character_native::build() == character_native::Build::Steam;
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto accessor = base + (steam ? 0x56d000 : 0x6f8a60);
            const auto calculator = base + (steam ? 0x632cb0 : 0x7d9000);
            const auto vtable = field<uintptr_t>(pawn, 0);
            constexpr unsigned char accessor_signature[]{0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x01, 0xff,
                                                         0x90, 0x40, 0x01, 0x00, 0x00, 0x48, 0x85, 0xc0};
            constexpr unsigned char calculator_signature[]{0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10,
                                                           0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x30};
            if (field<uintptr_t>(reinterpret_cast<void*>(vtable), 0x8c0) != accessor || field<int>(pawn, 0x514) != 0 ||
                std::memcmp(reinterpret_cast<void*>(accessor), accessor_signature, sizeof(accessor_signature)) != 0 ||
                std::memcmp(reinterpret_cast<void*>(calculator), calculator_signature, sizeof(calculator_signature)) !=
                    0 ||
                reinterpret_cast<void* (*)(UObject*)>(accessor)(pawn) != owner ||
                !is_uobject_valid_fast(static_cast<UObject*>(owner)))
            {
                throw std::runtime_error("kh02_aqua_experience_owner_signature");
            }
            const int difficulty = field<int>(owner, 0x8660);
            if (difficulty < 0 || difficulty > 3)
            {
                throw std::runtime_error("kh02_aqua_experience_difficulty");
            }
            // Same shipping modifier function used by the native award, including
            // difficulty and equipped EXP abilities. It does not write Sora's XP.
            return reinterpret_cast<int (*)(int, UObject*, int)>(calculator)(amount, pawn, difficulty);
        }
    } // namespace

    bool Kh02AquaProgression::prepare(UObject* context, Kh02EntryAssets& assets)
    {
        if (curve_ready)
        {
            return true;
        }
        constexpr std::array paths{curve_path,
                                   L"/Game/DataTable/Player/LevelUp/p_ex001_LevelUpData.p_ex001_LevelUpData",
                                   L"/Game/Load/Common/EnemyBaseStatus.EnemyBaseStatus"};
        std::array<UObject*, 3> objects{};
        bool ready = true;
        for (size_t index = 0; index < paths.size(); ++index)
        {
            objects[index] = find_loaded_runtime_asset(paths[index]);
            if (!is_uobject_valid_fast(objects[index]))
            {
                if (!requested[index])
                {
                    if (!request_runtime_asset_load(context, paths[index], nullptr,
                                                    0x4b4834c3 + static_cast<int>(index)))
                    {
                        throw std::runtime_error("kh02_experience_curve_request");
                    }
                    requested[index] = true;
                }
                ready = false;
            }
            else
            {
                assets.retain(objects[index]);
            }
        }
        if (!ready)
        {
            return false;
        }
        auto* object = objects[0];
        Kh02DataTableView table(object, L"/Script/TresGame.TresChrLevelUpDataTable", "kh02_aqua_level_curve_schema");
        constexpr std::array names{L"m_Exp", L"m_AttackPower", L"m_MagicPower", L"m_DefensePower", L"m_AbilityPoint"};
        std::array<FProperty*, names.size()> properties{};
        for (size_t index = 0; index < names.size(); ++index)
        {
            auto* property = table.schema->GetPropertyByNameInChain(names[index]);
            if (!property || property->GetClass().GetName() != L"IntProperty" ||
                property->GetElementSize() != sizeof(int) || property->GetArrayDim() != 1 ||
                property->GetOffset_Internal() != 8 + index * sizeof(int))
            {
                throw std::runtime_error("kh02_aqua_level_curve_field");
            }
            properties[index] = property;
        }
        for (size_t index = 0; index < curve.size(); ++index)
        {
            auto* row = table.find(std::to_wstring(index + kh02_level::first_level).c_str());
            if (!row)
            {
                throw std::runtime_error("kh02_aqua_level_curve_missing_row");
            }
            curve[index] = {
                *properties[0]->ContainerPtrToValuePtr<int>(row), *properties[1]->ContainerPtrToValuePtr<int>(row),
                *properties[2]->ContainerPtrToValuePtr<int>(row), *properties[3]->ContainerPtrToValuePtr<int>(row),
                *properties[4]->ContainerPtrToValuePtr<int>(row)};
        }
        kh02_level::validate(curve);
        for (size_t index = 1; index < objects.size(); ++index)
        {
            Kh02DataTableView shared(objects[index],
                                     index == 1 ? L"/Script/TresGame.TresChrLevelUpDataTable"
                                                : L"/Script/TresGame.TresEnemyBaseStatusDataTable",
                                     "kh02_shared_experience_schema");
            auto* property = shared.schema->GetPropertyByNameInChain(index == 1 ? L"m_Exp" : L"m_BaseExp");
            if (!property || property->GetClass().GetName() != L"IntProperty" ||
                property->GetElementSize() != sizeof(int) || property->GetArrayDim() != 1 ||
                property->GetOffset_Internal() != (index == 1 ? 8 : 12))
            {
                throw std::runtime_error("kh02_shared_experience_field");
            }
            auto& values = index == 1 ? sora_thresholds : enemy_base_exp;
            for (size_t row_index = 0; row_index < values.size(); ++row_index)
            {
                auto* row = shared.find(std::to_wstring(row_index + 1).c_str());
                if (!row)
                {
                    throw std::runtime_error("kh02_shared_experience_missing_row");
                }
                const int value = *property->ContainerPtrToValuePtr<int>(row);
                if (value < (index == 1 ? 0 : 1) || value > kh02_level::maximum_sora_exp ||
                    (row_index && value < values[row_index - 1]))
                {
                    throw std::runtime_error("kh02_shared_experience_curve_range");
                }
                values[row_index] = value;
            }
        }
        curve_ready = true;
        return true;
    }

    void Kh02AquaProgression::apply(UObject* pawn, int total)
    {
        if (!curve_ready)
        {
            throw std::runtime_error("kh02_aqua_level_curve_not_ready");
        }
        auto* base = base_parameters(pawn);
        const int level = kh02_level::level(curve, total);
        const auto index = static_cast<size_t>(level - kh02_level::first_level);
        const auto& row = curve[index];
        const int previous = field<int>(base, 0x40);
        field<int>(base, 0x40) = level;
        // Native status fields are next/current threshold, not current total XP.
        field<int>(base, 0x44) = level == kh02_level::last_level ? 0 : curve[index + 1].experience;
        field<int>(base, 0x48) = row.experience;
        field<int>(base, 0x5c) = row.strength;
        field<int>(base, 0x60) = row.magic;
        field<int>(base, 0x64) = row.defense;
        // Keep the integration's usable ability capacity; apply original growth
        // above level 50. HP/MP, equipment and permanent boosts remain native.
        field<int>(base, 0x68) = 200 + row.ap - curve.front().ap;
        parameters = make_uobject_weak(base);
        total_exp = total;
        if (previous != level)
        {
            character_level_revision.fetch_add(1, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] KH0.2 Aqua level={} exp={} previous_level={}\n"), level, total, previous);
        }
    }

    void Kh02AquaProgression::bind(UObject* pawn, uint64_t revision, Kh02StoryFlags& flags)
    {
        if (!is_uobject_valid_fast(pawn) || pawn->GetClassPrivate()->GetName() != L"p_ex002_Pawn_C")
        {
            hero = {};
            parameters = {};
            binding_revision = UINT64_MAX;
            return;
        }
        const auto level_revision = character_level_revision.load(std::memory_order_acquire);
        if (hero.Get() == pawn && binding_revision == revision && observed_level_revision == level_revision &&
            is_uobject_valid_fast(parameters.Get()))
        {
            return;
        }
        apply(pawn, flags.value(kh02_level::exp_flag, kh02_level::initial_exp));
        hero = make_uobject_weak(pawn);
        binding_revision = revision;
        observed_level_revision = character_level_revision.load(std::memory_order_acquire);
        flags.record_aqua_experience(total_exp);
    }

    auto Kh02AquaProgression::sora_level(void* save_owner) const -> int
    {
        if (!curve_ready || character_native::build() == character_native::Build::Unsupported ||
            !is_uobject_valid_fast(static_cast<UObject*>(save_owner)))
        {
            throw std::runtime_error("kh02_shared_experience_not_ready");
        }
        const int total = field<int>(save_owner, 0x8670);
        if (total < 0 || total > kh02_level::maximum_sora_exp)
        {
            throw std::runtime_error("kh02_sora_experience_range");
        }
        return std::clamp(static_cast<int>(std::upper_bound(sora_thresholds.begin(), sora_thresholds.end(), total) -
                                           sora_thresholds.begin()),
                          1, 99);
    }

    void Kh02AquaProgression::refresh_sora(UObject* pawn)
    {
        auto* owner = sora_refresh_owner.Get();
        if (!owner || !is_uobject_valid_fast(pawn) || pawn->GetClassPrivate()->GetName() != L"p_ex001_Pawn_C")
        {
            return;
        }
        // Validate the same owner and shipping layout before refreshing a parked
        // Sora. Fresh pawns also use this native path during initialization.
        (void)modified_award(pawn, owner, 0);
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto update = base + (character_native::build() == character_native::Build::Steam ? 0x5a3e10 : 0x736cd0);
        constexpr unsigned char signature[]{0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x83,
                                            0xb9, 0xd0, 0x04, 0x00, 0x00, 0x00, 0x48, 0x8b};
        if (field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(pawn, 0)), 0x1960) != update ||
            std::memcmp(reinterpret_cast<void*>(update), signature, sizeof(signature)) != 0)
        {
            throw std::runtime_error("kh02_sora_experience_refresh_signature");
        }
        reinterpret_cast<void (*)(UObject*)>(update)(pawn);
        sora_refresh_owner = {};
    }

    void Kh02AquaProgression::award(UObject* pawn, void* save_owner, int amount, int actual_level,
                                    int sora_battle_level, Kh02StoryFlags& flags)
    {
        const bool aqua = pawn->GetClassPrivate()->GetName() == L"p_ex002_Pawn_C";
        if (!curve_ready || (aqua && (hero.Get() != pawn || binding_revision == UINT64_MAX)) || actual_level < 1 ||
            actual_level > 99 || sora_battle_level < 1 || sora_battle_level > 99)
        {
            throw std::runtime_error("kh02_shared_experience_not_bound");
        }
        const int awarded = modified_award(pawn, save_owner, amount);
        const int actual_base = enemy_base_exp[actual_level - 1];
        const int aqua_award = aqua ? awarded : kh02_level::scale_award(awarded, actual_base, enemy_base_exp[49]);
        const int previous = flags.value(kh02_level::exp_flag, kh02_level::initial_exp);
        const int next = kh02_level::add_experience(previous, aqua_award);
        const int sora_award =
            aqua ? kh02_level::scale_award(awarded, actual_base, enemy_base_exp[sora_battle_level - 1]) : awarded;
        // Validate both banks before committing either. The native Sora path
        // handles his own award when he is active; Aqua must not call that path,
        // which would overwrite her base parameters with Sora's level.
        (void)sora_level(save_owner);
        if (aqua)
        {
            apply(pawn, next);
            const auto total = static_cast<int64_t>(field<int>(save_owner, 0x8670)) + sora_award;
            field<int>(save_owner, 0x8670) = static_cast<int>(std::min<int64_t>(total, kh02_level::maximum_sora_exp));
            sora_refresh_owner = make_uobject_weak(static_cast<UObject*>(save_owner));
        }
        flags.record_aqua_experience(next);
        RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] KH0.2 shared EXP aqua_award={} aqua_total={} "
                                                   "sora_award={} active_aqua={} enemy_level={} sora_enemy_level={}\n"),
                                               aqua_award, next, sora_award, aqua, actual_level, sora_battle_level);
    }

    auto Kh02AquaProgression::experience(UObject* pawn) const -> std::optional<int>
    {
        return hero.Get() == pawn && is_uobject_valid_fast(parameters.Get()) ? std::optional<int>{total_exp}
                                                                             : std::nullopt;
    }
} // namespace kh3ap
