#include "kh3ap/features/enemies/boss_remap_policy.h"
#include "kh3ap/features/enemies/boss_profile.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace kh3ap;
    {
        const std::wstring tt = L"/Game/Blueprints/Enemy/e_dw401_s0/e_dw401_Pawn_s0.e_dw401_Pawn_s0_C";
        const std::wstring data = L"/Game/Blueprints/Enemy/e_ex353/e_ex353_Pawn.e_ex353_Pawn_C";
        const std::wstring kg = L"/Game/Blueprints/Enemy/e_ex308/e_ex308_Pawn.e_ex308_Pawn_C";
        const std::wstring corona = L"/Game/Blueprints/Enemy/e_ex721/e_ex721_Pawn.e_ex721_Pawn_C";
        std::unordered_map<std::wstring, std::wstring> pairs{{tt, data}, {corona, data}, {kg, data}};
        assert(remove_incompatible_boss_arena_replacements(pairs) == 0);
        assert(pairs.at(tt) == data && pairs.at(corona) == data && pairs.at(kg) == data);
        for (const auto* story : {L"/Game/Blueprints/Enemy/e_ex309/e_ex309_Pawn.e_ex309_Pawn_C",
                L"/Game/Blueprints/Enemy/e_ex309_s10/e_ex309_Pawn_s10.e_ex309_Pawn_s10_C"})
        {
            pairs[tt] = story;
            pairs[corona] = story;
            assert(remove_incompatible_boss_arena_replacements(pairs) == 0);
            assert(pairs.at(tt) == story);
            assert(pairs.at(corona) == story);
        }
    }
    constexpr auto egs = boss_native::select_profile(0x60547d11, 0xaebf000);
    constexpr auto steam = boss_native::select_profile(0x66743f34, 0xa969000);
    static_assert(egs.supported && !egs.steam && steam.supported && steam.steam);
    static_assert(!boss_native::select_profile(0, 0).supported);
    static_assert(!boss_native::select_profile(0x60547d11, 0xa969000).supported);
    static_assert(!boss_native::select_profile(0x66743f34, 0xaebf000).supported);
    static_assert(!boss_native::select_profile(0x66743f35, 0xa969000).supported);
    static_assert(egs.grim_mode_rva == 0xeadac0 && steam.grim_mode_rva == 0xc40270);
    static_assert(egs.grim_mode_bytes[21] == 0x88 && egs.grim_mode_bytes[25] == 0x00);
    assert(needs_grim_guardianess_normal_mode(L"DemonTide", grim_guardianess_class));
    assert(!needs_grim_guardianess_normal_mode(grim_guardianess_class, grim_guardianess_class));
    assert(!needs_grim_guardianess_normal_mode(L"", grim_guardianess_class));
    assert(!needs_grim_guardianess_normal_mode(L"DemonTide", L"e_ex721_Pawn_C"));
    assert(!needs_grim_guardianess_normal_mode(grim_guardianess_class, L"other_boss"));
    constexpr auto rising_task = L"/Game/Blueprints/Enemy/e_ex721/BT/e_ex721_BT.e_ex721_BT";
    constexpr auto rising_move = L"/Game/Blueprints/Enemy/e_ex721/BT/e_ex721_Move4_Rising.e_ex721_Move4_Rising_C";
    assert(is_grim_guardianess_tower_rising_task(rising_task, rising_move));
    assert(!is_grim_guardianess_tower_rising_task(L"TresBTTask_MoveTo_43", rising_move));
    assert(!is_grim_guardianess_tower_rising_task(rising_task, L"ordinary_move"));
    assert(!is_grim_guardianess_tower_rising_task(std::wstring{rising_task} + L"_1", rising_move));
    static_assert(egs.grim_move_execute_rva == 0x622770 && steam.grim_move_execute_rva == 0x4abce0);
    static_assert(egs.grim_move_memory_size_bytes[1] == 0x28);
    static_assert(egs.grim_teleport_rva == 0x43f7b40 && steam.grim_teleport_rva == 0x3eaf560);
    static_assert(egs.grim_validate_action_rva == 0x5fd100 && steam.grim_validate_action_rva == 0x47fb20);
    static_assert(steam.grim_move_memory_size_bytes[1] == 0x28);
    static_assert(steam.grim_mode_bytes[21] == 0x88 && steam.grim_mode_bytes[25] == 0x00);
    static_assert(steam.grim_tower_teleport_caller_rva + steam.grim_tower_teleport_caller_bytes.size() ==
                  steam.grim_tower_teleport_return_rva);
    constexpr auto flash = L"/Game/Blueprints/Enemy/e_ex721/BT/e_ex721_Attack7_Flash_T.e_ex721_Attack7_Flash_T_C";
    assert(is_grim_guardianess_flash_action(rising_task, flash));
    assert(!is_grim_guardianess_flash_action(L"another_tree", flash));
    assert(!is_grim_guardianess_flash_action(rising_task, rising_move));
    static_assert(egs.grim_tower_teleport_caller_rva + egs.grim_tower_teleport_caller_bytes.size() ==
                  egs.grim_tower_teleport_return_rva);
    // The return-address filter must target exactly the end of the checked
    // lethal-damage call, never every invocation of the shared partner query.
    static_assert(egs.lethal_return_rva == egs.lethal_caller_rva + egs.lethal_caller_bytes.size());
    static_assert(steam.lethal_return_rva == steam.lethal_caller_rva + steam.lethal_caller_bytes.size());
    static_assert(egs.phase_rva != steam.phase_rva && egs.ansem_tick_rva != steam.ansem_tick_rva &&
                  egs.target_rva != steam.target_rva && egs.partners_rva != steam.partners_rva);
    const auto remind_c_level = L"/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_C.kg_05_enemy_Area_C:PersistentLevel";
    assert(is_kg_boss_reveal_level(remind_c_level, L'C'));
    assert(!is_kg_boss_reveal_level(remind_c_level, L'B'));
    assert(is_kg_boss_reveal_level(L"/Game/Levels/kg/kg_02/umap/kg_02_enemy_Area_B.kg_02_enemy_Area_B:PersistentLevel", L'B'));
    assert(!is_kg_boss_reveal_level(L"/Game/Levels/fz/fz_06/fz_06.fz_06:PersistentLevel", L'C'));
    assert(!is_kg_boss_reveal_level(std::wstring{remind_c_level}+L".UnrelatedActor", L'C'));
    assert(std::none_of(kg_boss_reveal_events.begin(), kg_boss_reveal_events.end(), [](const auto& event) {
        return event.name == L"kg_02_gameflow_REvt_Hide_ALL_Enemes" || event.name == L"ReceiveTick";
    }));
    assert(needs_remind_ansem_phase_driver(remind_xigbar_class, L"Terranort"));
    assert(!needs_remind_ansem_phase_driver(remind_xigbar_class, remind_xigbar_class));
    assert(!needs_remind_ansem_phase_driver(remind_xigbar_class,
        L"/Game/Blueprints/Enemy/e_ex305/e_ex305_Pawn.e_ex305_Pawn_C"));
    assert(!needs_remind_ansem_phase_driver(L"other_arena", L"Terranort"));
    assert(!needs_remind_ansem_phase_driver(remind_xigbar_class, L""));
    assert(!remind_ansem_retreat_due(19.9f, 91));
    assert(remind_ansem_retreat_due(20.0f, 100));
    assert(remind_ansem_retreat_due(0.25f, 90));
    assert(remind_ansem_retreat_due(5.0f, 1));
    assert(!remind_ansem_retreat_due(0.0f, -1));
    const auto larxene = L"/Game/Blueprints/Enemy/e_ex309/e_ex309_Pawn.e_ex309_Pawn_C";
    const auto remind_larxene = L"/Game/Blueprints/Enemy/e_ex309_s10/e_ex309_Pawn_s10.e_ex309_Pawn_s10_C";
    assert(needs_larxene_solo_phase(L"marluxia_source", larxene));
    assert(needs_larxene_solo_phase(L"other_source", remind_larxene));
    assert(!needs_larxene_solo_phase(larxene, larxene));
    assert(!needs_larxene_solo_phase(remind_larxene, remind_larxene));
    assert(!needs_larxene_solo_phase(L"", larxene));
    assert(!needs_larxene_solo_phase(L"source", L"e_ex309_Pawn_C"));
    assert(is_story_terranort_class(L"/Game/Blueprints/Enemy/e_ex325/e_ex325_Pawn.e_ex325_Pawn_C"));
    assert(is_story_terranort_class(L"/Game/Blueprints/Enemy/e_ex325_s0/e_ex325_Pawn_s0.e_ex325_Pawn_s0_C"));
    assert(is_story_terranort_class(L"/Game/Blueprints/Enemy/e_ex325_s10/e_ex325_Pawn_s10.e_ex325_Pawn_s10_C"));
    assert(!is_story_terranort_class(L"/Game/Blueprints/Enemy/e_ex362/e_ex362_Pawn.e_ex362_Pawn_C"));
    assert(!is_story_terranort_class(L"/Game/Blueprints/Enemy/e_ex326_s0/e_ex326_Pawn_s0.e_ex326_Pawn_s0_C"));
    std::unordered_map<std::wstring, std::wstring> replacements;
    const std::wstring marshmallow = L"/Game/Blueprints/Enemy/e_fz903/e_fz903_Pawn.e_fz903_Pawn_C";
    const std::wstring grim = L"/Game/Blueprints/Enemy/e_ex721/e_ex721_Pawn.e_ex721_Pawn_C";
    const std::wstring skoll = L"/Game/Blueprints/Enemy/e_ex731/e_ex731_Pawn.e_ex731_Pawn_C";
    const std::wstring data_xemnas = L"/Game/Blueprints/Enemy/e_ex353/e_ex353_Pawn.e_ex353_Pawn_C";
    const std::wstring kg_marluxia = L"/Game/Blueprints/Enemy/e_ex308/e_ex308_Pawn.e_ex308_Pawn_C";
    replacements.emplace(marshmallow, data_xemnas);
    replacements.emplace(kg_marluxia, data_xemnas);
    replacements.emplace(data_xemnas, marshmallow);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 1);
    assert(!replacements.contains(marshmallow));
    assert(replacements.at(kg_marluxia) == data_xemnas);
    assert(replacements.at(data_xemnas) == marshmallow);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
    replacements.clear();
    replacements.emplace(marshmallow, grim);
    replacements.emplace(skoll, grim);
    replacements.emplace(grim, marshmallow);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 1);
    assert(!replacements.contains(marshmallow));
    assert(replacements.at(skoll) == grim);
    assert(replacements.at(grim) == marshmallow);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
    const std::wstring king = L"/Game/Blueprints/Enemy/e_ex711/e_ex711_Pawn.e_ex711_Pawn_C";
    replacements.emplace(marshmallow, king);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 1);
    replacements.emplace(marshmallow, skoll);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
    assert(replacements.at(marshmallow) == skoll);
    const std::wstring lump = L"/Game/Blueprints/Enemy/e_ex701/e_ex701_Pawn.e_ex701_Pawn_C";
    replacements.emplace(lump, grim);
    replacements.insert_or_assign(grim, lump);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 1);
    assert(!replacements.contains(lump));
    assert(replacements.at(skoll) == grim);
    assert(replacements.at(grim) == lump);
    replacements.emplace(lump, king);
    assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
    assert(replacements.at(lump) == king);
    for (const auto* data_boss : {
            L"/Game/Blueprints/Enemy/e_ex352/e_ex352_Pawn.e_ex352_Pawn_C",
            L"/Game/Blueprints/Enemy/e_ex357/e_ex357_Pawn.e_ex357_Pawn_C"})
    {
        replacements.clear();
        replacements.emplace(lump, data_boss);
        replacements.emplace(marshmallow, data_boss);
        replacements.emplace(kg_marluxia, data_boss);
        replacements.emplace(data_boss, lump);
        assert(remove_incompatible_boss_arena_replacements(replacements) == 2);
        assert(!replacements.contains(lump));
        assert(!replacements.contains(marshmallow));
        assert(replacements.at(kg_marluxia) == data_boss);
        assert(replacements.at(data_boss) == lump);
        assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
    }
    for (const int id : {351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 367})
    {
        const auto pawn = L"e_ex" + std::to_wstring(id);
        const auto data_boss = L"/Game/Blueprints/Enemy/" + pawn + L"/" + pawn + L"_Pawn." + pawn + L"_Pawn_C";
        replacements.clear();
        replacements.emplace(marshmallow, data_boss);
        replacements.emplace(kg_marluxia, data_boss);
        replacements.emplace(data_boss, marshmallow);
        assert(remove_incompatible_boss_arena_replacements(replacements) == 1);
        assert(!replacements.contains(marshmallow));
        assert(replacements.at(kg_marluxia) == data_boss);
        assert(replacements.at(data_boss) == marshmallow);
    }
    for (const auto* story_boss : {
            L"/Game/Blueprints/Enemy/e_ex316/e_ex316_Pawn.e_ex316_Pawn_C",
            L"/Game/Blueprints/Enemy/e_ex308/e_ex308_Pawn.e_ex308_Pawn_C",
            L"/Game/Blueprints/Enemy/e_ex308_s10/e_ex308_Pawn_s10.e_ex308_Pawn_s10_C"})
    {
        replacements.clear();
        replacements.emplace(lump, story_boss);
        replacements.emplace(marshmallow, story_boss);
        assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
        assert(replacements.at(lump) == story_boss);
    }
    for (const auto* titan_class : {
            L"/Game/Blueprints/Enemy/e_he902/e_he902_Pawn.e_he902_Pawn_C",
            L"/Game/Blueprints/Enemy/e_he904/e_he904_Pawn.e_he904_Pawn_C"})
    {
        for (const auto* young_xehanort : {
                L"/Game/Blueprints/Enemy/e_ex302/e_ex302_Pawn.e_ex302_Pawn_C",
                L"/Game/Blueprints/Enemy/e_ex302_s10/e_ex302_Pawn_s10.e_ex302_Pawn_s10_C",
                L"/Game/Blueprints/Enemy/e_ex351/e_ex351_Pawn.e_ex351_Pawn_C"})
        {
            replacements.clear();
            replacements.emplace(titan_class, young_xehanort);
            replacements.emplace(skoll, young_xehanort);
            replacements.emplace(young_xehanort, titan_class);
            assert(remove_incompatible_boss_arena_replacements(replacements) == 1);
            assert(!replacements.contains(titan_class));
            assert(replacements.at(skoll) == young_xehanort);
            assert(replacements.at(young_xehanort) == titan_class);
            assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
        }
        replacements.emplace(titan_class, skoll);
        assert(remove_incompatible_boss_arena_replacements(replacements) == 0);
        assert(replacements.at(titan_class) == skoll);
    }
    replacements.clear();
    const std::wstring titan = L"/Game/Blueprints/Enemy/e_he902/e_he902_Pawn.e_he902_Pawn_C";
    const std::wstring marluxia = L"/Game/Blueprints/Enemy/e_ex308/e_ex308_Pawn.e_ex308_Pawn_C";
    replacements.emplace(titan, marluxia);
    for (size_t index = 0; index < lich_boss_classes.size(); ++index)
    {
        const std::wstring lich{lich_boss_classes[index]};
        assert(is_lich_boss_class(lich));
        replacements.emplace(lich, marluxia);
        replacements.emplace(L"other_source_" + std::to_wstring(index), lich);
    }
    assert(remove_lich_boss_replacements(replacements) == 14);
    assert(replacements.size() == 1);
    assert(replacements.at(titan) == marluxia);
    assert(remove_lich_boss_replacements(replacements) == 0);
    assert(!is_lich_boss_class(marluxia));
    assert(!is_lich_boss_class(L"e_ex047_Pawn_C"));
    assert(!is_lich_boss_class(L"/Game/Blueprints/Enemy/e_ex047_fake/e_ex047_Pawn.e_ex047_Pawn_C"));
    assert(!is_lich_boss_class(L""));
    replacements.emplace(std::wstring{final_xehanort_class}, marluxia);
    replacements.emplace(L"other_final_source", std::wstring{final_xehanort_class});
    replacements.emplace(std::wstring{scala_first_xehanort_class}, titan);
    assert(remove_final_xehanort_replacements(replacements) == 2);
    assert(replacements.at(std::wstring{scala_first_xehanort_class}) == titan);
    assert(replacements.at(titan) == marluxia);
    assert(remove_final_xehanort_replacements(replacements) == 0);
    const auto scala_entity = L"/Game/Levels/bt/bt_04/umap/bt_04_enemy_02.bt_04_enemy_02:PersistentLevel.e_ex301_Pawn_s0_1";
    assert(is_scala_final_transition(scala_entity, scala_first_xehanort_class, L"bt_04_en_02_Group_01"));
    assert(!is_scala_final_transition(scala_entity, final_xehanort_class, L"bt_04_en_02_Group_01"));
    assert(!is_scala_final_transition(scala_entity, scala_first_xehanort_class, L"bt_04_en_02_Group_02"));
    assert(!is_scala_final_transition(L"other_entity", scala_first_xehanort_class, L"bt_04_en_02_Group_01"));
    const auto kg_tide_entity = L"/Game/Levels/kg/kg_01/umap/kg_01_enemy_02.kg_01_enemy_02:PersistentLevel.e_dw401_Pawn_s1";
    assert(is_kg_demon_tide_transition(kg_tide_entity, kg_demon_tide_class, L"kg_01_02_en_Group_01"));
    assert(!is_kg_demon_tide_transition(kg_tide_entity, scala_first_xehanort_class, L"kg_01_02_en_Group_01"));
    assert(!is_kg_demon_tide_transition(scala_entity, kg_demon_tide_class, L"kg_01_02_en_Group_01"));
    assert(!is_kg_demon_tide_transition(kg_tide_entity, kg_demon_tide_class, L"kg_01_02_en_Group_02"));
    std::cout << "Lich source/replacement exclusion and existing-seed preservation passed\n";
}
