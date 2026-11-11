#include "kh3ap/features/enemies/boss_hp_limit_policy.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace kh3ap;
    const auto remind_marluxia = L"/Game/Blueprints/Enemy/e_ex308_s10/e_ex308_Pawn_s10.e_ex308_Pawn_s10_C";
    const auto base_larxene = L"/Game/Blueprints/Enemy/e_ex309/e_ex309_Pawn.e_ex309_Pawn_C";
    const auto remind_entity = L"/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_B.kg_05_enemy_Area_B:PersistentLevel.e_ex308_Pawn_s10_3";
    const auto base_entity = L"/Game/Levels/kg/kg_02/umap/kg_02_enemy_Area_B.kg_02_enemy_Area_B:PersistentLevel.e_ex309_Pawn_5";
    assert(is_luxord_partner_slot(remind_entity, remind_marluxia));
    assert(is_luxord_partner_slot(base_entity, base_larxene));
    assert(!is_luxord_partner_slot(remind_entity, base_larxene));
    assert(!is_luxord_partner_slot(L"/Game/Levels/fz/fz_01/fz_01:PersistentLevel.boss", remind_marluxia));
    assert(!is_luxord_partner_slot(base_entity, L"/Game/Blueprints/Enemy/e_ex325/e_ex325_Pawn.e_ex325_Pawn_C"));
    assert(is_luxord_source_entity(L"/Game/Levels/kg/kg_02/umap/kg_02_enemy_Area_B.kg_02_enemy_Area_B:PersistentLevel.e_ex307_Pawn"));
    assert(is_luxord_source_entity(L"/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_B.kg_05_enemy_Area_B:PersistentLevel.e_ex307_Pawn_s10_1"));
    assert(!is_luxord_source_entity(remind_entity));
    assert(!luxord_native_defeat_observed(0, 0));
    assert(!luxord_native_defeat_observed(1, 0));
    assert(!luxord_native_defeat_observed(2, 1));
    assert(luxord_native_defeat_observed(1, 1));
    assert(luxord_native_defeat_observed(2, 2));
    assert(!luxord_partner_hp_floor_released(0, 0, false));
    assert(luxord_partner_hp_floor_released(0, 0, true));
    assert(!luxord_partner_hp_floor_released(1, 0, true));
    assert(!luxord_partner_hp_floor_released(0, 1, true));
    assert(luxord_partner_hp_floor_released(1, 1, false));
    const auto master = std::find_if(boss_hp_limit_policies.begin(), boss_hp_limit_policies.end(),
            [](const auto& policy) { return policy.class_name == L"e_ex301_Pawn_s0_C"; });
    assert(master != boss_hp_limit_policies.end());
    assert(master->kind == BossHpLimitKind::explicit_lock);
    assert(master->variant_class_path.empty());
    const auto armored = std::find_if(boss_hp_limit_policies.begin(), boss_hp_limit_policies.end(),
            [](const auto& policy) { return policy.class_name == L"e_ex773_Pawn_C"; });
    assert(armored != boss_hp_limit_policies.end());
    assert(armored->kind == BossHpLimitKind::explicit_lock);
    assert(uses_boss_hp_limit_bypass(
            L"/Game/Blueprints/Enemy/e_ex309_s10/e_ex309_Pawn_s10.e_ex309_Pawn_s10_C",
            armored->class_path));
    assert(!uses_boss_hp_limit_bypass(armored->class_path, armored->class_path));
    assert(uses_boss_hp_limit_bypass(
            L"/Game/Blueprints/Enemy/e_fz903/e_fz903_Pawn.e_fz903_Pawn_C", master->class_path));
    assert(!uses_boss_hp_limit_bypass(master->class_path, master->class_path));
    assert(!uses_boss_hp_limit_bypass(L"source",
            L"/Game/Blueprints/Enemy/e_ex301/e_ex301_Pawn.e_ex301_Pawn_C"));
    assert(!uses_boss_hp_limit_bypass(L"source",
            L"/Game/Blueprints/Enemy/e_ex367/e_ex367_Pawn.e_ex367_Pawn_C"));
    const auto tide = std::find_if(boss_hp_limit_policies.begin(), boss_hp_limit_policies.end(),
            [](const auto& policy) { return policy.name == L"Demon Tide"; });
    assert(tide != boss_hp_limit_policies.end());
    assert(tide->release_phase_property == L"m_bIsCyclone");
    assert(uses_boss_hp_limit_bypass(L"source", tide->class_path));
    assert(!uses_boss_hp_limit_bypass(tide->class_path, tide->class_path));
    assert(uses_boss_hp_limit_bypass(L"source",
            L"/Game/Blueprints/Enemy/e_dw401_s0/e_dw401_Pawn_s0.e_dw401_Pawn_s0_C"));
    assert(tide_scripted_finish_hp(4000, 3600, 4000, 85) == 3600);
    assert(tide_scripted_finish_hp(3600, 3400, 4000, 85) == 0);
    assert(tide_scripted_finish_hp(3600, 3439, 4000, 85) == 0);
    assert(tide_scripted_finish_hp(3600, 3440, 4000, 85) == 3440);
    assert(tide_scripted_finish_hp(0, 4000, 4000, 85) == 4000);
    assert(tide_scripted_finish_hp(3000, 3200, 4000, 85) == 3200);
    assert(tide_scripted_finish_hp(3000, 3000, 4000, 85) == 3000);
    assert(tide_scripted_finish_hp(4000, 3000, 0, 85) == 3000);
    assert(tide_scripted_finish_hp(4000, 3000, 4000, 100) == 3000);
    // Xigbar/Dark Riku's live one-HP lock reads bit 5 directly; do not confuse it with
    // bit 6, which preserves the requirement for a player finishing blow.
    assert(!preserves_explicit_boss_hp_limit(BossHpLimitKind::explicit_lock));
    for (auto kind : {BossHpLimitKind::coop_phase, BossHpLimitKind::data_marluxia,
                      BossHpLimitKind::data_young_xehanort})
        assert(preserves_explicit_boss_hp_limit(kind));
    assert(is_boss_hp_limit_getter(BossHpLimitKind::explicit_lock, explicit_hp_limit_getter, 0x630));
    assert(!is_boss_hp_limit_getter(BossHpLimitKind::explicit_lock, explicit_hp_limit_getter, 0x634));
    assert(!is_boss_hp_limit_getter(BossHpLimitKind::explicit_lock,
                                  std::span{explicit_hp_limit_getter}.first(11), 0x630));
    auto wrong_explicit_bit = explicit_hp_limit_getter;
    wrong_explicit_bit[8] = 6;
    assert(!is_boss_hp_limit_getter(BossHpLimitKind::explicit_lock, wrong_explicit_bit, 0x630));
    for (const auto& policy : boss_hp_limit_policies)
    {
        assert(uses_boss_hp_limit_bypass(
                L"/Game/Blueprints/Enemy/e_he902/e_he902_Pawn.e_he902_Pawn_C",
                policy.class_path));
        assert(!uses_boss_hp_limit_bypass(L"", policy.class_path));
        assert(!uses_boss_hp_limit_bypass(policy.class_path, policy.class_path));
        // Matching a short class name is insufficient to opt into a bypass.
        assert(!uses_boss_hp_limit_bypass(L"source", policy.class_name));
    }
    const auto riku = std::find_if(boss_hp_limit_policies.begin(), boss_hp_limit_policies.end(),
            [](const auto& policy) { return policy.name == L"Dark Riku"; });
    assert(riku != boss_hp_limit_policies.end());
    assert(riku->kind == BossHpLimitKind::explicit_lock);
    assert(!uses_boss_hp_limit_bypass(L"source",
            L"/Game/Blueprints/Enemy/e_ex313/e_ex313_Pawn_s10.e_ex313_Pawn_s10_C"));
    assert(!uses_boss_hp_limit_bypass(L"source",
            L"/Game/Blueprints/Enemy/e_ex361/e_ex361_Pawn.e_ex361_Pawn_C"));
    assert(matches_hp_limit_signature(xion_hp_setter, xion_hp_setter));
    assert(!matches_hp_limit_signature(std::span{xion_hp_setter}.first(xion_hp_setter.size() - 1), xion_hp_setter));
    auto altered_setter = xion_hp_setter;
    altered_setter[109] ^= 1; // Different minimum-HP comparison must fail closed.
    assert(!matches_hp_limit_signature(altered_setter, xion_hp_setter));
    assert(!matches_hp_limit_signature(xion_hp_setter, base_hp_setter));
    assert(!matches_hp_limit_signature(std::span{base_hp_setter}.first(base_hp_setter.size() - 1), base_hp_setter));
    assert(matches_hp_limit_signature(steam_xion_hp_setter, steam_xion_hp_setter));
    assert(!matches_hp_limit_signature(std::span{steam_xion_hp_setter}.first(98), steam_xion_hp_setter));
    assert(!matches_hp_limit_signature(steam_xion_hp_setter, xion_hp_setter));
    auto steam_setter_wrong_clamp = steam_xion_hp_setter;
    steam_setter_wrong_clamp[40] = 2;
    assert(!matches_hp_limit_signature(steam_setter_wrong_clamp, steam_xion_hp_setter));
    const auto xion = std::find_if(boss_hp_limit_policies.begin(), boss_hp_limit_policies.end(),
            [](const auto& policy) { return policy.name == L"Xion"; });
    assert(xion != boss_hp_limit_policies.end());
    assert(xion->kind == BossHpLimitKind::explicit_lock);
    assert(!uses_boss_hp_limit_bypass(L"source",
            L"/Game/Blueprints/Enemy/e_ex310/e_ex310_Pawn_s10.e_ex310_Pawn_s10_C"));
    assert(!uses_boss_hp_limit_bypass(L"source",
            L"/Game/Blueprints/Enemy/e_ex359/e_ex359_Pawn.e_ex359_Pawn_C"));
    for (const auto& policy : boss_hp_limit_policies)
    {
        if (policy.variant_class_path.empty()) continue;
        assert(uses_boss_hp_limit_bypass(L"source", policy.variant_class_path));
        assert(!uses_boss_hp_limit_bypass(policy.variant_class_path, policy.variant_class_path));
        assert(!uses_boss_hp_limit_bypass(L"", policy.variant_class_path));
        assert(!uses_boss_hp_limit_bypass(L"source", policy.variant_class_name));
        assert(matches_boss_hp_limit_policy(policy, policy.variant_class_path));
    }
    // Ordinary data getters and the excluded Luxord/armored Xehanort remain out.
    for (auto path : {
                 L"/Game/Blueprints/Enemy/e_ex359/e_ex359_Pawn.e_ex359_Pawn_C",
                 L"/Game/Blueprints/Enemy/e_ex358/e_ex358_Pawn.e_ex358_Pawn_C",
                 L"/Game/Blueprints/Enemy/e_ex356/e_ex356_Pawn.e_ex356_Pawn_C",
                 L"/Game/Blueprints/Enemy/e_ex771_s0/e_ex771_Pawn_s0.e_ex771_Pawn_s0_C"})
    {
        assert(!uses_boss_hp_limit_bypass(L"source", path));
    }
    assert(hp_limit_virtual_slot(hp_limit_exec_thunk) == 0x2090u);
    assert(!hp_limit_virtual_slot(std::span{hp_limit_exec_thunk}.first(46)));
    auto thunk = hp_limit_exec_thunk;
    thunk[35] = 0x91;
    assert(!hp_limit_virtual_slot(thunk));
    thunk = hp_limit_exec_thunk;
    thunk[33] = 0xe9; // Another hook or unfamiliar build must fail closed.
    assert(!hp_limit_virtual_slot(thunk));
    assert(is_coop_phase_hp_limit_getter(coop_phase_hp_limit_getter, 0x630));
    assert(!is_coop_phase_hp_limit_getter(coop_phase_hp_limit_getter, 0x634));
    assert(!is_coop_phase_hp_limit_getter(
            std::span{coop_phase_hp_limit_getter}.first(29), 0x630));
    auto getter = coop_phase_hp_limit_getter;
    getter[9] = 0x75; // Different phase condition is not the supported getter.
    assert(!is_coop_phase_hp_limit_getter(getter, 0x630));
    getter = coop_phase_hp_limit_getter;
    getter[26] = 6; // Do not confuse the player-attack protection flag.
    assert(!is_coop_phase_hp_limit_getter(getter, 0x630));
    for (const auto kind : {BossHpLimitKind::data_marluxia, BossHpLimitKind::data_young_xehanort})
    {
        const std::span<const uint8_t> signature = kind == BossHpLimitKind::data_marluxia
                ? std::span<const uint8_t>{data_marluxia_hp_limit_getter}
                : std::span<const uint8_t>{data_young_xehanort_hp_limit_getter};
        assert(boss_hp_limit_getter_size(kind) == signature.size());
        assert(is_boss_hp_limit_getter(kind, signature, 0x630));
        assert(identify_boss_hp_limit_build(kind, signature, 0x630) == BossHpLimitBuild::egs);
        assert(!is_boss_hp_limit_getter(kind, signature, 0x634));
        assert(!is_boss_hp_limit_getter(kind, signature.first(signature.size() - 1), 0x630));
        assert(!is_boss_hp_limit_getter(kind, coop_phase_hp_limit_getter, 0x630));
    }
    auto data_getter = data_marluxia_hp_limit_getter;
    data_getter[137] = 0x75; // Invert scripted-state branch: unknown behavior.
    assert(!is_boss_hp_limit_getter(BossHpLimitKind::data_marluxia, data_getter, 0x630));
    auto young_getter = data_young_xehanort_hp_limit_getter;
    young_getter[10] ^= 1; // A different predicate target must fail closed.
    assert(!is_boss_hp_limit_getter(BossHpLimitKind::data_young_xehanort, young_getter, 0x630));
    auto predicate = data_young_xehanort_hp_limit_predicate;
    assert(matches_hp_limit_signature(predicate, data_young_xehanort_hp_limit_predicate));
    predicate[12] ^= 1; // A different scripted state field is unsupported.
    assert(!matches_hp_limit_signature(predicate, data_young_xehanort_hp_limit_predicate));

    // Steam fixtures captured from native CDOs. Use a synthetic image base to
    // verify relative branches without depending on the process's ASLR base.
    struct SteamCase
    {
        BossHpLimitKind kind;
        std::span<const uint8_t> code;
        uintptr_t rva;
    };
    constexpr uintptr_t image_base = 0x140000000;
    constexpr uintptr_t fallback = image_base + 0x5ad100;
    const std::array<SteamCase, 3> steam_cases{{
            {BossHpLimitKind::coop_phase, steam_coop_phase_hp_limit_getter, 0xe2d1c0},
            {BossHpLimitKind::data_marluxia, steam_data_marluxia_hp_limit_getter, 0xb2e590},
            {BossHpLimitKind::data_young_xehanort, steam_data_young_xehanort_hp_limit_getter, 0xa07120},
    }};
    for (const auto& test : steam_cases)
    {
        assert(boss_hp_limit_getter_size(test.kind) >= test.code.size());
        assert(identify_boss_hp_limit_build(test.kind, test.code, 0x630) == BossHpLimitBuild::steam);
        assert(!identify_boss_hp_limit_build(test.kind, test.code, 0x634));
        assert(!identify_boss_hp_limit_build(test.kind, test.code.first(test.code.size() - 1), 0x630));
        assert(matches_steam_hp_limit_fallback(test.kind, test.code, image_base + test.rva,
                                             fallback, explicit_hp_limit_getter, 0x630));
        assert(!matches_steam_hp_limit_fallback(test.kind, test.code, image_base + test.rva,
                                              fallback + 8, explicit_hp_limit_getter, 0x630));
        auto wrong_flag = explicit_hp_limit_getter;
        wrong_flag[8] = 6;
        assert(!matches_steam_hp_limit_fallback(test.kind, test.code, image_base + test.rva,
                                              fallback, wrong_flag, 0x630));
    }
    auto steam_coop = steam_coop_phase_hp_limit_getter;
    steam_coop[18] = 0xe8; // A call instead of the verified tail jump is unsafe.
    assert(!identify_boss_hp_limit_build(BossHpLimitKind::coop_phase, steam_coop, 0x630));
    assert(hp_limit_relative_target(steam_data_young_xehanort_hp_limit_getter,
                                   image_base + 0xa07120, 9, 0xe8) == image_base + 0xa07150);
    assert(!hp_limit_relative_target(steam_data_young_xehanort_hp_limit_getter, image_base, 9, 0xe9));
    assert(!hp_limit_relative_target({}, image_base, 9, 0xe8));
    assert(!hp_limit_relative_target(std::span{steam_data_young_xehanort_hp_limit_getter}.first(13),
                                    image_base, 9, 0xe8));
    assert(matches_hp_limit_signature(steam_data_young_xehanort_hp_limit_predicate,
                                     boss_hp_limit_predicate_signature(BossHpLimitBuild::steam)));
    assert(!matches_hp_limit_signature(steam_data_young_xehanort_hp_limit_predicate,
                                      boss_hp_limit_predicate_signature(BossHpLimitBuild::egs)));
    auto steam_predicate = steam_data_young_xehanort_hp_limit_predicate;
    steam_predicate[12] ^= 1;
    assert(!matches_hp_limit_signature(steam_predicate,
                                      boss_hp_limit_predicate_signature(BossHpLimitBuild::steam)));
    std::cout << "Boss HP limit policy tests passed\n";
}
