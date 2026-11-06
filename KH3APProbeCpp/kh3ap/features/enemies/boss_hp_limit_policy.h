#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace kh3ap
{
    inline constexpr auto x64_relative_branch_bytes = sizeof(uint8_t) + sizeof(int32_t);

    inline constexpr auto boss_hp_full_percent = 100;
    inline constexpr auto boss_hp_percent_scale = 100.0f;

    // Luxord's card game must finish before either of his two arena partners
    // can die. Otherwise their source death script skips the card-game phase.
    [[nodiscard]] inline auto is_luxord_partner_slot(std::wstring_view entity,
                                                     std::wstring_view source) -> bool
    {
        const bool base = entity.starts_with(
            L"/Game/Levels/kg/kg_02/umap/kg_02_enemy_Area_B.kg_02_enemy_Area_B:PersistentLevel.");
        const bool remind =
            entity.starts_with(L"/Game/Levels/kg_DLC/kg_05/umap/"
                               L"kg_05_enemy_Area_B.kg_05_enemy_Area_B:PersistentLevel.");
        if (base && (source == L"/Game/Blueprints/Enemy/e_ex308/e_ex308_Pawn.e_ex308_Pawn_C" ||
                     source == L"/Game/Blueprints/Enemy/e_ex309/e_ex309_Pawn.e_ex309_Pawn_C"))
        {
            return true;
        }
        if (remind &&
            (source == L"/Game/Blueprints/Enemy/e_ex308_s10/e_ex308_Pawn_s10.e_ex308_Pawn_s10_C" ||
             source == L"/Game/Blueprints/Enemy/e_ex309_s10/e_ex309_Pawn_s10.e_ex309_Pawn_s10_C"))
        {
            return true;
        }
        return false;
    }

    [[nodiscard]] inline auto is_luxord_source_entity(std::wstring_view entity) -> bool
    {
        return entity.starts_with(
                   L"/Game/Levels/kg/kg_02/umap/"
                   L"kg_02_enemy_Area_B.kg_02_enemy_Area_B:PersistentLevel.e_ex307_Pawn") ||
               entity.starts_with(
                   L"/Game/Levels/kg_DLC/kg_05/umap/"
                   L"kg_05_enemy_Area_B.kg_05_enemy_Area_B:PersistentLevel.e_ex307_Pawn_s10");
    }

    [[nodiscard]] inline auto luxord_native_defeat_observed(int32_t spawns, int32_t kills) -> bool
    {
        return spawns > 0 && kills >= spawns;
    }

    enum class BossHpLimitKind
    {
        coop_phase,
        data_marluxia,
        data_young_xehanort,
        explicit_lock
    };
    enum class BossHpLimitBuild
    {
        egs,
        steam
    };

    struct BossHpLimitPolicy
    {
        std::wstring_view name;
        std::wstring_view class_path;
        std::wstring_view class_name;
        std::wstring_view native_cdo;
        BossHpLimitKind kind;
        std::wstring_view variant_class_path{};
        std::wstring_view variant_class_name{};
        // Applies to the primary class only; aliases can have a different end condition.
        std::wstring_view release_phase_property{};
    };

    // Native getter audit: phase-based floors plus arena-owned explicit one-HP locks.
    // Other eligible data bosses use the ordinary explicit-property getter.
    // Data Luxord is excluded from the randomizer pool and remains untouched.
    inline constexpr std::array<BossHpLimitPolicy, 10> boss_hp_limit_policies{{
        // Both Tide variants rely on their arena to finish the fight.
        // KG keeps its floor until cyclone mode begins; TT ends at its scripted cutoff.
        {L"Demon Tide", L"/Game/Blueprints/Enemy/e_dw401_s1/e_dw401_Pawn_s1.e_dw401_Pawn_s1_C",
         L"e_dw401_Pawn_s1_C", L"/Script/TresGame.Default__TresEnemyPawn_e_dw401",
         BossHpLimitKind::explicit_lock,
         L"/Game/Blueprints/Enemy/e_dw401_s0/e_dw401_Pawn_s0.e_dw401_Pawn_s0_C",
         L"e_dw401_Pawn_s0_C", L"m_bIsCyclone"},
        {L"Xigbar", L"/Game/Blueprints/Enemy/e_ex305/e_ex305_Pawn.e_ex305_Pawn_C",
         L"e_ex305_Pawn_C", L"/Script/TresGame.Default__TresEnemyPawn_e_ex305",
         BossHpLimitKind::explicit_lock,
         L"/Game/Blueprints/Enemy/e_ex305_s10/e_ex305_Pawn_s10.e_ex305_Pawn_s10_C",
         L"e_ex305_Pawn_s10_C"},
        {L"Dark Riku", L"/Game/Blueprints/Enemy/e_ex313/e_ex313_Pawn.e_ex313_Pawn_C",
         L"e_ex313_Pawn_C", L"/Script/TresGame.Default__TresEnemyPawn_e_ex313",
         BossHpLimitKind::explicit_lock,
         L"/Game/Blueprints/Enemy/e_ex313_s10/e_ex313_Pawn_s10.e_ex313_Pawn_s10_C",
         L"e_ex313_Pawn_s10_C"},
        {L"Xion", L"/Game/Blueprints/Enemy/e_ex310/e_ex310_Pawn.e_ex310_Pawn_C", L"e_ex310_Pawn_C",
         L"/Script/TresGame.Default__TresEnemyPawn_e_ex310", BossHpLimitKind::explicit_lock,
         L"/Game/Blueprints/Enemy/e_ex310_s10/e_ex310_Pawn_s10.e_ex310_Pawn_s10_C",
         L"e_ex310_Pawn_s10_C"},
        // Scala's first story fight hands off through its owning arena.
        // Away from Scala its explicit floor never releases. Only this
        // tracked replacement class participates; final/Data Xehanort and
        // native Scala instances retain their ordinary HP rules.
        {L"Master Xehanort (Scala ad Caelum)",
         L"/Game/Blueprints/Enemy/e_ex301_s0/e_ex301_Pawn_s0.e_ex301_Pawn_s0_C",
         L"e_ex301_Pawn_s0_C", L"/Script/TresGame.Default__TresEnemyPawn_e_ex301",
         BossHpLimitKind::explicit_lock},
        {L"Armored Xehanort (Re Mind)",
         L"/Game/Blueprints/Enemy/e_ex773/e_ex773_Pawn.e_ex773_Pawn_C", L"e_ex773_Pawn_C",
         L"/Script/TresGame.Default__TresEnemyPawn_e_ex773", BossHpLimitKind::explicit_lock},
        {L"Marluxia", L"/Game/Blueprints/Enemy/e_ex308/e_ex308_Pawn.e_ex308_Pawn_C",
         L"e_ex308_Pawn_C", L"/Script/TresGame.Default__TresEnemyPawn_e_ex308",
         BossHpLimitKind::coop_phase,
         L"/Game/Blueprints/Enemy/e_ex308_s10/e_ex308_Pawn_s10.e_ex308_Pawn_s10_C",
         L"e_ex308_Pawn_s10_C"},
        {L"Larxene", L"/Game/Blueprints/Enemy/e_ex309/e_ex309_Pawn.e_ex309_Pawn_C",
         L"e_ex309_Pawn_C", L"/Script/TresGame.Default__TresEnemyPawn_e_ex309",
         BossHpLimitKind::coop_phase,
         L"/Game/Blueprints/Enemy/e_ex309_s10/e_ex309_Pawn_s10.e_ex309_Pawn_s10_C",
         L"e_ex309_Pawn_s10_C"},
        {L"Data Marluxia", L"/Game/Blueprints/Enemy/e_ex357/e_ex357_Pawn.e_ex357_Pawn_C",
         L"e_ex357_Pawn_C", L"/Script/TresGame.Default__TresEnemyPawn_e_ex357",
         BossHpLimitKind::data_marluxia},
        {L"Data Young Xehanort", L"/Game/Blueprints/Enemy/e_ex351/e_ex351_Pawn.e_ex351_Pawn_C",
         L"e_ex351_Pawn_C", L"/Script/TresGame.Default__TresEnemyPawn_e_ex351",
         BossHpLimitKind::data_young_xehanort},
    }};

    // Blueprint variants share the native base vtable. Install one hook
    // per native class so aliases cannot chain or overwrite each other's hook.
    [[nodiscard]] inline auto matches_boss_hp_limit_policy(const BossHpLimitPolicy& policy,
                                                           std::wstring_view path) -> bool
    {
        return path == policy.class_path ||
               (!policy.variant_class_path.empty() && path == policy.variant_class_path);
    }

    [[nodiscard]] inline auto tide_scripted_finish_hp(int32_t current_hp, int32_t requested_hp,
                                                      int32_t max_hp, int32_t cutoff_percent)
        -> int32_t
    {
        // Native TT compares the truncated HP percentage with m_ttBattleEndHp.
        // Only turn a decrease into a finish; initialization/healing stay native.
        if (current_hp > 0 && requested_hp < current_hp && requested_hp > 0 && max_hp > 0 &&
            cutoff_percent > 0 && cutoff_percent < boss_hp_full_percent &&
            static_cast<int32_t>(static_cast<float>(requested_hp) / max_hp *
                                 boss_hp_percent_scale) <= cutoff_percent)
        {
            return 0;
        }
        return requested_hp;
    }

    [[nodiscard]] inline auto uses_boss_hp_limit_bypass(std::wstring_view source_class,
                                                        std::wstring_view replacement_class) -> bool
    {
        return !source_class.empty() && source_class != replacement_class &&
               std::any_of(boss_hp_limit_policies.begin(), boss_hp_limit_policies.end(),
                           [replacement_class](const auto& policy)
                           {
                               return matches_boss_hp_limit_policy(policy, replacement_class);
                           });
    }

    // Verified EGS/Steam IsConditionHpLimitOne exec thunk. Derive the virtual slot
    // from the reflected function, and reject unfamiliar machine code.
    inline constexpr size_t hp_limit_virtual_slot_operand_offset = 35;
    inline constexpr uint32_t hp_limit_vtable_min_byte_offset = 0x100;
    inline constexpr uint32_t hp_limit_vtable_max_byte_offset = 0x4000;
    inline constexpr uint32_t hp_limit_vtable_entry_alignment = 8;
    inline constexpr int32_t boss_hp_limit_flag_property_offset = 0x630;

    inline constexpr std::array<uint8_t, 47> hp_limit_exec_thunk{
        0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x42, 0x20, 0x45, 0x33,
        0xc9, 0x48, 0x85, 0xc0, 0x49, 0x8b, 0xd8, 0x41, 0x0f, 0x95, 0xc1, 0x4c,
        0x03, 0xc8, 0x4c, 0x89, 0x4a, 0x20, 0x48, 0x8b, 0x01, 0xff, 0x90, 0x90,
        0x20, 0x00, 0x00, 0x88, 0x03, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};

    [[nodiscard]] inline auto hp_limit_virtual_slot(std::span<const uint8_t> code)
        -> std::optional<uint32_t>
    {
        if (code.size() < hp_limit_exec_thunk.size())
        {
            return std::nullopt;
        }
        for (size_t index = 0; index < hp_limit_exec_thunk.size(); ++index)
        {
            if ((index < hp_limit_virtual_slot_operand_offset ||
                 index >= hp_limit_virtual_slot_operand_offset + sizeof(uint32_t)) &&
                code[index] != hp_limit_exec_thunk[index])
            {
                return std::nullopt;
            }
        }
        uint32_t slot{};
        std::memcpy(&slot, code.data() + hp_limit_virtual_slot_operand_offset, sizeof(slot));
        return slot >= hp_limit_vtable_min_byte_offset && slot <= hp_limit_vtable_max_byte_offset &&
                       slot % hp_limit_vtable_entry_alignment == 0
                   ? std::optional<uint32_t>{slot}
                   : std::nullopt;
    }

    // EGS Xion SetHitPoint clamps below 1 even when IsConditionHpLimitOne is false.
    // Full native signatures include the notification path and pinned relative operands.

    inline constexpr std::array<uint8_t, 249> xion_hp_setter{
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
        0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x01, 0x8b, 0xf2, 0x8b, 0xa9, 0xfc, 0x05, 0x00,
        0x00, 0x33, 0xff, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x90, 0x20, 0x18, 0x00, 0x00, 0x48, 0x8d,
        0x05, 0x0e, 0x88, 0xb8, 0xff, 0x48, 0x3b, 0xd0, 0x0f, 0x85, 0xb0, 0x00, 0x00, 0x00, 0x80,
        0xb9, 0x5d, 0x08, 0x00, 0x00, 0x12, 0x75, 0x0c, 0x44, 0x8b, 0x81, 0x1c, 0x06, 0x00, 0x00,
        0x45, 0x85, 0xc0, 0x7f, 0x1b, 0x48, 0x8b, 0x89, 0xd0, 0x04, 0x00, 0x00, 0x48, 0x85, 0xc9,
        0x74, 0x07, 0xe8, 0x7f, 0x83, 0xb4, 0xff, 0xeb, 0x05, 0x44, 0x8b, 0xc7, 0xeb, 0x03, 0x44,
        0x8b, 0xc0, 0x83, 0xfe, 0x01, 0x7d, 0x08, 0x41, 0xb8, 0x01, 0x00, 0x00, 0x00, 0xeb, 0x07,
        0x41, 0x3b, 0xf0, 0x44, 0x0f, 0x4c, 0xc6, 0x48, 0x8d, 0x8b, 0x48, 0x04, 0x00, 0x00, 0x44,
        0x89, 0x83, 0xfc, 0x05, 0x00, 0x00, 0x48, 0x8b, 0x01, 0x4c, 0x8b, 0x88, 0x20, 0x02, 0x00,
        0x00, 0x48, 0x8d, 0x05, 0x72, 0x6e, 0xb9, 0xff, 0x4c, 0x3b, 0xc8, 0x75, 0x4f, 0x48, 0x8b,
        0x81, 0x58, 0x01, 0x00, 0x00, 0x48, 0x85, 0xc0, 0x74, 0x07, 0x48, 0x8b, 0xb8, 0x70, 0x02,
        0x00, 0x00, 0x48, 0x85, 0xff, 0x74, 0x1b, 0x48, 0x8b, 0x47, 0x48, 0x48, 0x8d, 0x4f, 0x48,
        0x4c, 0x8b, 0x88, 0x20, 0x02, 0x00, 0x00, 0x48, 0x8d, 0x05, 0x7f, 0xf9, 0x9c, 0xff, 0x4c,
        0x3b, 0xc8, 0x75, 0x1c, 0x48, 0x8b, 0x5c, 0x24, 0x30, 0x48, 0x8b, 0x6c, 0x24, 0x38, 0x48,
        0x8b, 0x74, 0x24, 0x40, 0x48, 0x83, 0xc4, 0x20, 0x5f, 0xc3, 0xff, 0xd2, 0xe9, 0x76, 0xff,
        0xff, 0xff, 0x8b, 0xd5, 0x41, 0xff, 0xd1, 0xeb, 0xdd};

    inline constexpr std::array<uint8_t, 95> base_hp_setter{
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec,
        0x20, 0x48, 0x8b, 0x01, 0x8b, 0xfa, 0x8b, 0xb1, 0xfc, 0x05, 0x00, 0x00, 0x48, 0x8b,
        0xd9, 0xff, 0x90, 0x20, 0x18, 0x00, 0x00, 0x44, 0x8b, 0xc0, 0x85, 0xff, 0x79, 0x05,
        0x45, 0x33, 0xc0, 0xeb, 0x07, 0x41, 0x3b, 0xf8, 0x44, 0x0f, 0x4c, 0xc7, 0x48, 0x8d,
        0x8b, 0x48, 0x04, 0x00, 0x00, 0x44, 0x89, 0x83, 0xfc, 0x05, 0x00, 0x00, 0x48, 0x8b,
        0x01, 0x8b, 0xd6, 0x48, 0x8b, 0x5c, 0x24, 0x30, 0x48, 0x8b, 0x74, 0x24, 0x38, 0x48,
        0x83, 0xc4, 0x20, 0x5f, 0x48, 0xff, 0xa0, 0x20, 0x02, 0x00, 0x00};

    // Steam Xion setter: same clamp and notification path; parent setter is identical.

    inline constexpr std::array<uint8_t, 99> steam_xion_hp_setter{
        0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20,
        0x48, 0x8b, 0x01, 0x8b, 0xfa, 0x8b, 0xb1, 0xfc, 0x05, 0x00, 0x00, 0x48, 0x8b, 0xd9, 0xff,
        0x90, 0x20, 0x18, 0x00, 0x00, 0x44, 0x8b, 0xc0, 0x83, 0xff, 0x01, 0x7d, 0x08, 0x41, 0xb8,
        0x01, 0x00, 0x00, 0x00, 0xeb, 0x07, 0x41, 0x3b, 0xf8, 0x44, 0x0f, 0x4c, 0xc7, 0x48, 0x8d,
        0x8b, 0x48, 0x04, 0x00, 0x00, 0x44, 0x89, 0x83, 0xfc, 0x05, 0x00, 0x00, 0x48, 0x8b, 0x01,
        0x8b, 0xd6, 0x48, 0x8b, 0x5c, 0x24, 0x30, 0x48, 0x8b, 0x74, 0x24, 0x38, 0x48, 0x83, 0xc4,
        0x20, 0x5f, 0x48, 0xff, 0xa0, 0x20, 0x02, 0x00, 0x00};

    // TresEnemyXIIIBCoopPawnBase overrides the property getter: phases 1/2
    // always return true, while later phases read m_bIsHpLimitOne normally.
    inline constexpr size_t coop_phase_member_operand_offset = 3;
    inline constexpr size_t coop_hp_limit_flag_operand_offset = 20;
    inline constexpr int32_t max_coop_member_offset_exclusive = 0x10000;

    inline constexpr std::array<uint8_t, 30> coop_phase_hp_limit_getter{
        0x0f, 0xb6, 0x81, 0xc8, 0x0c, 0x00, 0x00, 0x3c, 0x01, 0x74, 0x04, 0x84, 0xc0, 0x75, 0x03,
        0xb0, 0x01, 0xc3, 0x8b, 0x81, 0x30, 0x06, 0x00, 0x00, 0xc1, 0xe8, 0x05, 0x24, 0x01, 0xc3};

    [[nodiscard]] inline auto is_coop_phase_hp_limit_getter(std::span<const uint8_t> code,
                                                            int32_t reflected_hp_limit_offset)
        -> bool
    {
        if (code.size() < coop_phase_hp_limit_getter.size())
        {
            return false;
        }
        for (size_t index = 0; index < coop_phase_hp_limit_getter.size(); ++index)
        {
            if ((index < coop_phase_member_operand_offset ||
                 index >= coop_phase_member_operand_offset + sizeof(int32_t)) &&
                (index < coop_hp_limit_flag_operand_offset ||
                 index >= coop_hp_limit_flag_operand_offset + sizeof(int32_t)) &&
                code[index] != coop_phase_hp_limit_getter[index])
            {
                return false;
            }
        }
        int32_t phase_offset{}, flag_offset{};
        std::memcpy(&phase_offset, code.data() + coop_phase_member_operand_offset,
                    sizeof(phase_offset));
        std::memcpy(&flag_offset, code.data() + coop_hp_limit_flag_operand_offset,
                    sizeof(flag_offset));
        return flag_offset == reflected_hp_limit_offset && flag_offset > 0 &&
               phase_offset > flag_offset && phase_offset < max_coop_member_offset_exclusive;
    }

    // Verified EGS data-boss getters, including their image-relative operands.
    // Marluxia adds a scripted flag at 0xde4; Young Xehanort calls a predicate
    // using his scripted state at 0xec5. Both native paths already recognize
    // Deadly Blow. These signatures preserve their original code for fallback.

    inline constexpr std::array<uint8_t, 164> data_marluxia_hp_limit_getter{
        0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x01, 0x48, 0x8b, 0xd9, 0xff, 0x90, 0xc0,
        0x08, 0x00, 0x00, 0x4c, 0x8b, 0xc0, 0x48, 0x85, 0xc0, 0x74, 0x64, 0x8b, 0x40, 0x0c, 0x45,
        0x33, 0xc9, 0x03, 0xc0, 0xd1, 0xf8, 0x3b, 0x05, 0xe2, 0xd4, 0xff, 0x07, 0x7d, 0x14, 0x48,
        0x63, 0xc8, 0x48, 0x8b, 0x05, 0xca, 0xd4, 0xff, 0x07, 0x48, 0x8d, 0x14, 0x49, 0x48, 0x8d,
        0x0c, 0xd0, 0xeb, 0x03, 0x49, 0x8b, 0xc9, 0x8b, 0x41, 0x08, 0xc1, 0xe8, 0x1d, 0xa8, 0x01,
        0x75, 0x31, 0x49, 0x8b, 0xc1, 0x4c, 0x89, 0x4c, 0x24, 0x30, 0x49, 0x39, 0x80, 0x88, 0x07,
        0x00, 0x00, 0x75, 0x20, 0x45, 0x38, 0x88, 0x4c, 0x08, 0x00, 0x00, 0x75, 0x17, 0x41, 0xf6,
        0x80, 0x1c, 0x79, 0x95, 0x00, 0x01, 0x74, 0x0d, 0x41, 0xf7, 0x80, 0xb4, 0x7a, 0x95, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x75, 0x15, 0x8b, 0x83, 0xe4, 0x0d, 0x00, 0x00, 0xc1, 0xe8, 0x02,
        0xa8, 0x01, 0x74, 0x08, 0xb0, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3, 0x8b, 0x83, 0x30,
        0x06, 0x00, 0x00, 0xc1, 0xe8, 0x05, 0x24, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};

    inline constexpr std::array<uint8_t, 43> data_young_xehanort_hp_limit_getter{
        0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x32, 0x2a, 0x00, 0x00, 0x84,
        0xc0, 0x74, 0x08, 0xb0, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3, 0x8b, 0x83, 0x30, 0x06,
        0x00, 0x00, 0xc1, 0xe8, 0x05, 0x24, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};

    inline constexpr std::array<uint8_t, 148> data_young_xehanort_hp_limit_predicate{
        0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x01, 0x0f, 0xb6, 0x99, 0xc5, 0x0e, 0x00,
        0x00, 0xff, 0x90, 0xc0, 0x08, 0x00, 0x00, 0x48, 0x8b, 0xd0, 0x48, 0x85, 0xc0, 0x74, 0x6d,
        0x8b, 0x40, 0x0c, 0x45, 0x33, 0xc9, 0x03, 0xc0, 0xd1, 0xf8, 0x3b, 0x05, 0x8e, 0x80, 0x12,
        0x08, 0x7d, 0x13, 0x48, 0x98, 0x48, 0x8d, 0x0c, 0x40, 0x48, 0x8b, 0x05, 0x73, 0x80, 0x12,
        0x08, 0x4c, 0x8d, 0x04, 0xc8, 0xeb, 0x03, 0x4d, 0x8b, 0xc1, 0x41, 0x8b, 0x40, 0x08, 0xc1,
        0xe8, 0x1d, 0xa8, 0x01, 0x75, 0x3a, 0x49, 0x8b, 0xc1, 0x4c, 0x89, 0x4c, 0x24, 0x30, 0x48,
        0x39, 0x82, 0x88, 0x07, 0x00, 0x00, 0x75, 0x29, 0x44, 0x38, 0x8a, 0x4c, 0x08, 0x00, 0x00,
        0x75, 0x20, 0xf6, 0x82, 0x1c, 0x79, 0x95, 0x00, 0x01, 0x74, 0x17, 0xf7, 0x82, 0xb4, 0x7a,
        0x95, 0x00, 0x00, 0x10, 0x00, 0x00, 0x0f, 0xb6, 0xc3, 0x41, 0x0f, 0x45, 0xc1, 0x48, 0x83,
        0xc4, 0x20, 0x5b, 0xc3, 0x0f, 0xb6, 0xc3, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};

    // Captured from EGS Xigbar, Dark Riku and Xion CDOs and live Corona replacements.
    // Unlike the cooperative phase getters, this reads the explicit HP floor.

    inline constexpr std::array<uint8_t, 12> explicit_hp_limit_getter{
        0x8b, 0x81, 0x30, 0x06, 0x00, 0x00, 0xc1, 0xe8, 0x05, 0x24, 0x01, 0xc3};

    [[nodiscard]] inline auto preserves_explicit_boss_hp_limit(BossHpLimitKind kind) -> bool
    {
        return kind != BossHpLimitKind::explicit_lock;
    }

    [[nodiscard]] inline auto boss_hp_limit_getter_size(BossHpLimitKind kind) -> size_t
    {
        switch (kind)
        {
        case BossHpLimitKind::coop_phase:
            return coop_phase_hp_limit_getter.size();
        case BossHpLimitKind::data_marluxia:
            return data_marluxia_hp_limit_getter.size();
        case BossHpLimitKind::data_young_xehanort:
            return data_young_xehanort_hp_limit_getter.size();
        case BossHpLimitKind::explicit_lock:
            return explicit_hp_limit_getter.size();
        }
        return 0;
    }

    [[nodiscard]] inline auto matches_hp_limit_signature(std::span<const uint8_t> code,
                                                         std::span<const uint8_t> signature) -> bool
    {
        return code.size() >= signature.size() &&
               std::equal(signature.begin(), signature.end(), code.begin());
    }

    [[nodiscard]] inline auto is_egs_boss_hp_limit_getter(BossHpLimitKind kind,
                                                          std::span<const uint8_t> code,
                                                          int32_t reflected_hp_limit_offset) -> bool
    {
        if (kind == BossHpLimitKind::coop_phase)
        {
            return is_coop_phase_hp_limit_getter(code, reflected_hp_limit_offset);
        }
        if (reflected_hp_limit_offset != boss_hp_limit_flag_property_offset)
        {
            return false;
        }
        switch (kind)
        {
        case BossHpLimitKind::explicit_lock:
            return matches_hp_limit_signature(code, explicit_hp_limit_getter);
        case BossHpLimitKind::data_marluxia:
            return matches_hp_limit_signature(code, data_marluxia_hp_limit_getter);
        case BossHpLimitKind::data_young_xehanort:
            return matches_hp_limit_signature(code, data_young_xehanort_hp_limit_getter);
        default:
            return false;
        }
    }

    // Steam uses tail jumps to TresCharPawnBase's ordinary property getter.
    // Captured from the native CDOs; all relative branch operands are pinned
    // to the inspected build, and the shared fallback is checked separately.

    inline constexpr std::array<uint8_t, 23> steam_coop_phase_hp_limit_getter{
        0x0f, 0xb6, 0x81, 0xc8, 0x0c, 0x00, 0x00, 0x3c, 0x01, 0x74, 0x04, 0x84,
        0xc0, 0x75, 0x03, 0xb0, 0x01, 0xc3, 0xe9, 0x29, 0xff, 0x77, 0xff};

    inline constexpr std::array<uint8_t, 121> steam_data_marluxia_hp_limit_getter{
        0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x01, 0x48, 0x8b, 0xd9, 0xff, 0x90,
        0xc0, 0x08, 0x00, 0x00, 0x4c, 0x8b, 0xc0, 0x48, 0x85, 0xc0, 0x74, 0x3d, 0x8b, 0x50,
        0x0c, 0x03, 0xd2, 0xd1, 0xfa, 0x3b, 0x15, 0x75, 0xca, 0xbf, 0x07, 0x7d, 0x14, 0x48,
        0x63, 0xca, 0x48, 0x8d, 0x14, 0x49, 0x48, 0x8b, 0x0d, 0x59, 0xca, 0xbf, 0x07, 0x48,
        0x8d, 0x04, 0xd1, 0xeb, 0x02, 0x33, 0xc0, 0x8b, 0x40, 0x08, 0xc1, 0xe8, 0x1d, 0xa8,
        0x01, 0x75, 0x0e, 0xb2, 0x0c, 0x49, 0x8b, 0xc8, 0xe8, 0x7d, 0xcb, 0x37, 0x00, 0x84,
        0xc0, 0x75, 0x15, 0x8b, 0x83, 0xe4, 0x0d, 0x00, 0x00, 0xc1, 0xe8, 0x02, 0xa8, 0x01,
        0x74, 0x08, 0xb0, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3, 0x48, 0x8b, 0xcb, 0x48,
        0x83, 0xc4, 0x20, 0x5b, 0xe9, 0xf7, 0xea, 0xa7, 0xff};

    inline constexpr std::array<uint8_t, 39> steam_data_young_xehanort_hp_limit_getter{
        0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0x22, 0x00, 0x00,
        0x00, 0x84, 0xc0, 0x74, 0x08, 0xb0, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3,
        0x48, 0x8b, 0xcb, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0xb9, 0x5f, 0xba, 0xff};

    inline constexpr std::array<uint8_t, 139> steam_data_young_xehanort_hp_limit_predicate{
        0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x01, 0x0f, 0xb6, 0x99, 0xc5, 0x0e,
        0x00, 0x00, 0xff, 0x90, 0xc0, 0x08, 0x00, 0x00, 0x4c, 0x8b, 0xc0, 0x48, 0x85, 0xc0,
        0x74, 0x64, 0x8b, 0x40, 0x0c, 0x03, 0xc0, 0x48, 0x89, 0x7c, 0x24, 0x30, 0xd1, 0xf8,
        0x33, 0xff, 0x3b, 0x05, 0xaa, 0x3e, 0xd2, 0x07, 0x7d, 0x13, 0x48, 0x98, 0x48, 0x8d,
        0x0c, 0x40, 0x48, 0x8b, 0x05, 0x8f, 0x3e, 0xd2, 0x07, 0x48, 0x8d, 0x14, 0xc8, 0xeb,
        0x03, 0x48, 0x8b, 0xd7, 0x8b, 0x42, 0x08, 0xc1, 0xe8, 0x1d, 0xa8, 0x01, 0x75, 0x20,
        0xb2, 0x0c, 0x49, 0x8b, 0xc8, 0xe8, 0xb2, 0x3f, 0x4a, 0x00, 0x84, 0xc0, 0x0f, 0xb6,
        0xcb, 0x0f, 0x45, 0xcf, 0x48, 0x8b, 0x7c, 0x24, 0x30, 0x0f, 0xb6, 0xc1, 0x48, 0x83,
        0xc4, 0x20, 0x5b, 0xc3, 0x48, 0x8b, 0x7c, 0x24, 0x30, 0x0f, 0xb6, 0xc3, 0x48, 0x83,
        0xc4, 0x20, 0x5b, 0xc3, 0x0f, 0xb6, 0xc3, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};

    [[nodiscard]] inline auto identify_boss_hp_limit_build(BossHpLimitKind kind,
                                                           std::span<const uint8_t> code,
                                                           int32_t reflected_hp_limit_offset)
        -> std::optional<BossHpLimitBuild>
    {
        if (is_egs_boss_hp_limit_getter(kind, code, reflected_hp_limit_offset))
        {
            return BossHpLimitBuild::egs;
        }
        if (reflected_hp_limit_offset != boss_hp_limit_flag_property_offset)
        {
            return std::nullopt;
        }
        std::span<const uint8_t> signature;
        switch (kind)
        {
        case BossHpLimitKind::coop_phase:
            signature = steam_coop_phase_hp_limit_getter;
            break;
        case BossHpLimitKind::data_marluxia:
            signature = steam_data_marluxia_hp_limit_getter;
            break;
        case BossHpLimitKind::data_young_xehanort:
            signature = steam_data_young_xehanort_hp_limit_getter;
            break;
        default:
            return std::nullopt;
        }
        return matches_hp_limit_signature(code, signature) ? std::optional{BossHpLimitBuild::steam}
                                                           : std::nullopt;
    }

    [[nodiscard]] inline auto is_boss_hp_limit_getter(BossHpLimitKind kind,
                                                      std::span<const uint8_t> code,
                                                      int32_t reflected_hp_limit_offset) -> bool
    {
        return identify_boss_hp_limit_build(kind, code, reflected_hp_limit_offset).has_value();
    }

    [[nodiscard]] inline auto hp_limit_relative_target(std::span<const uint8_t> code,
                                                       uintptr_t address, size_t opcode_offset,
                                                       uint8_t opcode) -> std::optional<uintptr_t>
    {
        if (opcode_offset >= code.size() ||
            code.size() - opcode_offset < x64_relative_branch_bytes ||
            code[opcode_offset] != opcode)
        {
            return std::nullopt;
        }
        int32_t displacement{};
        std::memcpy(&displacement, code.data() + opcode_offset + 1, sizeof(displacement));
        return address + opcode_offset + x64_relative_branch_bytes + displacement;
    }

    [[nodiscard]] inline auto
    matches_steam_hp_limit_fallback(BossHpLimitKind kind, std::span<const uint8_t> code,
                                    uintptr_t address, uintptr_t fallback_address,
                                    std::span<const uint8_t> fallback_code,
                                    int32_t reflected_hp_limit_offset) -> bool
    {
        if (identify_boss_hp_limit_build(kind, code, reflected_hp_limit_offset) !=
                BossHpLimitBuild::steam ||
            !matches_hp_limit_signature(fallback_code, explicit_hp_limit_getter))
        {
            return false;
        }
        const size_t jump_offset = kind == BossHpLimitKind::coop_phase      ? 18
                                   : kind == BossHpLimitKind::data_marluxia ? 116
                                                                            : 34;
        const auto destination = hp_limit_relative_target(code, address, jump_offset, 0xe9);
        return destination && *destination == fallback_address;
    }

    [[nodiscard]] inline auto boss_hp_limit_predicate_signature(BossHpLimitBuild build)
        -> std::span<const uint8_t>
    {
        return build == BossHpLimitBuild::steam
                   ? std::span<const uint8_t>{steam_data_young_xehanort_hp_limit_predicate}
                   : std::span<const uint8_t>{data_young_xehanort_hp_limit_predicate};
    }

} // namespace kh3ap
