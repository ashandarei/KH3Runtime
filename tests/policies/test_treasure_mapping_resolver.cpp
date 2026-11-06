#include "kh3ap/features/treasure/treasure_mapping_resolver.h"

#include <cassert>
#include <string>
#include <unordered_map>

namespace
{
    using Lookup = std::unordered_map<std::wstring, std::wstring>;

    auto expect_mapping(std::wstring_view actor, std::wstring_view source,
                        std::wstring_view reason = L"") -> void
    {
        const std::wstring world{source.substr(0, source.find(L'_'))};
        const auto key = L"Treasure" + world + L":" + std::wstring{source};
        const auto result = kh3ap::TreasureMappingResolver::infer_from_actor_name(actor, {{std::wstring{source}, key}});
        assert(result && result->source_id == source && result->bridge_key == key && result->world_code == world);
        if (!reason.empty()) assert(result->mapping_reason == reason);
    }

    auto test_shipped_actor_remaps() -> void
    {
        // Actor spellings from the shipped treasure sublevels. Several names are
        // zero-based or refer to a different row than their visible numeric suffix.
        struct Fixture { const wchar_t* actor; const wchar_t* source; };
        const Fixture fixtures[]{
            {L"g_he_TreasureBox_C he_02_treasure:PersistentLevel.he_TreasureBox_0", L"HE_SBOX_017"},
            {L"g_he_TreasureBox_C he_03_treasure:PersistentLevel.g_he_TreasureBox_0", L"HE_SBOX_026"},
            {L"g_ts_TreasureBoxL_C ts_01_treasure:PersistentLevel.g_TreasureBoxL1", L"TS_LBOX_002"},
            {L"g_ts_TreasureBoxL_C ts_01_treasure:PersistentLevel.g_TreasureBoxL3", L"TS_LBOX_001"},
            {L"g_ts_TreasureBox_C ts_01_treasure:PersistentLevel.g_ts_TreasureBox1", L"TS_SBOX_002"},
            {L"g_ts_TreasureBox_C ts_01_treasure:PersistentLevel.g_ts_TreasureBox_C_1", L"TS_SBOX_001"},
            {L"g_ts_TreasureBox_C ts_02_treasure:PersistentLevel.g_ts_TreasureBox_1", L"TS_SBOX_006"},
            {L"g_mi_TreasureBoxL_C mi_02_treasure:PersistentLevel.g_mi_TreasureBoxL_0", L"MI_LBOX_004"},
            {L"g_mi_TreasureBox_C mi_04_treasure:PersistentLevel.g_mi_TreasureBox_0", L"MI_SBOX_019"},
            {L"g_kg_TreasureBoxL_C kg_02_treasure:PersistentLevel.01_g_kg_TreasureBoxL", L"KG_LBOX_002"},
            {L"g_kg_TreasureBox_C kg_01_treasure:PersistentLevel.g_kg_TreasureBox_2", L"KG_SBOX_004"},
            {L"g_kg_TreasureBox_C kg_02_treasure:PersistentLevel.02_g_kg_TreasureBox", L"KG_SBOX_001"},
            {L"g_kg_TreasureBox_C kg_02_treasure:PersistentLevel.03_g_kg_TreasureBox", L"KG_SBOX_002"},
            {L"g_kg_TreasureBox_C kg_02_treasure:PersistentLevel.04_g_kg_TreasureBox", L"KG_SBOX_003"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_l_2", L"BT_LBOX_002"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_2", L"BT_SBOX_001"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_3", L"BT_SBOX_002"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_4", L"BT_SBOX_003"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_5", L"BT_SBOX_004"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_6", L"BT_SBOX_005"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_7", L"BT_SBOX_006"},
            {L"g_bt_TreasureBox_C bt_07_treasure:PersistentLevel.g_bt_TreasureBox_8", L"BT_SBOX_007"},
            {L"g_bt_TreasureBox_C bt_08_treasure:PersistentLevel.g_bt_TreasureBox_l_2", L"BT_LBOX_001"},
        };
        for (const auto& fixture : fixtures)
        {
            expect_mapping(fixture.actor, fixture.source);
            // Animation notifications resolve the owning actor from the mesh's full name.
            expect_mapping(std::wstring{fixture.actor} + L".Mesh", fixture.source);
        }
    }

    auto test_name_formats_and_precedence() -> void
    {
        expect_mapping(L"g_he_TreasureBox_HE_LBOX_003_C_0", L"HE_LBOX_003", L"validated_actor_explicit_source_id");
        expect_mapping(L"g_ra_TreasureSBOX_009_C_2", L"RA_SBOX_009", L"validated_actor_treasure_box_suffix");
        expect_mapping(L"Actor:PersistentLevel.ra_TreasurelBox_2", L"RA_LBOX_002", L"validated_actor_compact_box_suffix");
        expect_mapping(L"Actor:PersistentLevel.ra_TreasuresBox_2", L"RA_SBOX_002", L"validated_actor_compact_box_suffix");
        expect_mapping(L"Actor:PersistentLevel.ra_TreasureBox_2", L"RA_SBOX_002", L"validated_actor_compact_small_suffix");
        expect_mapping(L"Actor:PersistentLevel.07_g_kg_TreasureBoxL", L"KG_LBOX_007", L"validated_actor_prefixed_instance_map");
        expect_mapping(L"Actor:PersistentLevel.07_g_kg_TreasureBox", L"KG_SBOX_007", L"validated_actor_prefixed_instance_map");
        expect_mapping(L"g_he_TreasureBoxL_C:PersistentLevel.g_TreasureBoxL_Area_3", L"HE_LBOX_003", L"validated_actor_class_world_area_large_suffix");
        expect_mapping(L"g_he_TreasureBoxL_C:PersistentLevel.g_TreasureBoxL3", L"HE_LBOX_003", L"validated_actor_class_world_instance_suffix");
        expect_mapping(L"g_he_TreasureBox_C:PersistentLevel.g_TreasureBox_Area_3", L"HE_SBOX_003", L"validated_actor_class_world_area_small_suffix");
        expect_mapping(L"g_he_TreasureBox_C:PersistentLevel.g_TreasureBox3", L"HE_SBOX_003", L"validated_actor_class_world_instance_suffix");
        expect_mapping(L"Actor:PersistentLevel.g_ca_TreasureBoxL_17", L"CA_LBOX_017", L"validated_actor_instance_map");
        expect_mapping(L"Actor:PersistentLevel.g_ca_TreasureBoxL_Area_17", L"CA_LBOX_017", L"validated_actor_instance_area_map");
        expect_mapping(L"Actor:PersistentLevel.g_ca_TreasureBox_17", L"CA_SBOX_017", L"validated_actor_instance_map");
        expect_mapping(L"Actor:PersistentLevel.g_ca_TreasureBox_Area_17", L"CA_SBOX_017", L"validated_actor_instance_area_map");
        expect_mapping(L"TreasureBox /Game/Levels/ew/ew_02:PersistentLevel.Chest", L"EW_LBOX_001", L"validated_final_world_single_large_chest");
        expect_mapping(L"SpecialShip_TreasureBox_C /Game/Maps/ca/ca_99:PersistentLevel.Chest", L"CA_SBOX_025", L"validated_caribbean_special_ship_actor");

        // An explicit ID wins over a conflicting instance suffix, even if the
        // instance exists in the seed and the explicit ID does not.
        const auto conflicting = L"g_he_TreasureBox_HE_LBOX_003_C /Game/Levels/he/he_01:PersistentLevel.g_he_TreasureBoxL_4";
        expect_mapping(conflicting, L"HE_LBOX_003", L"validated_actor_explicit_source_id");
        assert(!kh3ap::TreasureMappingResolver::infer_from_actor_name(conflicting, {{L"HE_LBOX_004", L"wrong"}}));
        expect_mapping(L"g_he_TreasureBoxL_C:PersistentLevel.g_TreasureBoxL_12_3", L"HE_LBOX_003", L"validated_actor_class_world_area_large_suffix");
        expect_mapping(L"Actor:PersistentLevel.g_he_TreasureBoxL_12_3", L"HE_LBOX_012", L"validated_actor_instance_map");

        const std::wstring bounded = L"g_he_TreasureBox_HE_LBOX_003 ignored g_he_TreasureBox_HE_LBOX_004";
        expect_mapping(std::wstring_view{bounded}.substr(0, 29), L"HE_LBOX_003");
    }

    auto test_lookup_policy() -> void
    {
        using Resolver = kh3ap::TreasureMappingResolver;
        const auto zero = L"g_ra_TreasureBox_0";
        expect_mapping(zero, L"RA_SBOX_000", L"validated_actor_instance_map");
        expect_mapping(zero, L"RA_SBOX_001", L"validated_actor_instance_map_zero_based_first_box");
        const auto rough = Resolver::infer_from_actor_name(zero, {});
        assert(rough && rough->source_id == L"RA_SBOX_000");
        assert(rough->mapping_reason == L"validated_actor_instance_map_rough_slot_data_unavailable");
        assert(!Resolver::infer_from_actor_name(zero, {{L"RA_SBOX_002", L"wrong"}}));

        const auto remapped = L"g_mi_TreasureBox_C mi_04_treasure:PersistentLevel.g_mi_TreasureBox_0";
        const Lookup both{{L"MI_SBOX_019", L"remapped"}, {L"MI_SBOX_000", L"exact"}};
        assert(Resolver::infer_from_actor_name(remapped, both)->bridge_key == L"remapped");
        expect_mapping(remapped, L"MI_SBOX_000", L"validated_actor_instance_map");
        expect_mapping(remapped, L"MI_SBOX_001", L"validated_actor_instance_map_zero_based_first_box");
        const auto rough_remap = Resolver::infer_from_actor_name(remapped, {});
        assert(rough_remap && rough_remap->source_id == L"MI_SBOX_019");
        assert(rough_remap->mapping_reason == L"validated_actor_instance_map_rough_slot_data_unavailable");

        for (const auto invalid : {L"", L"NotAChest", L"g_he_TreasureBox_MI_LBOX_003", L"g_he_TreasureBoxL_-1", L"g_HE_TreasureBoxL_3"})
            assert(!Resolver::infer_from_actor_name(invalid, both));
    }
}

int main()
{
    test_shipped_actor_remaps();
    test_name_formats_and_precedence();
    test_lookup_policy();
    const std::unordered_map<std::wstring, std::wstring> treasure_lookup{
            {L"KG_SBOX_001", L"TreasureKG:KG_SBOX_001"},
            {L"KG_SBOX_002", L"TreasureKG:KG_SBOX_002"},
            {L"KG_SBOX_003", L"TreasureKG:KG_SBOX_003"},
            {L"MI_SBOX_001", L"TreasureMI:MI_SBOX_001"},
            {L"MI_SBOX_019", L"TreasureMI:MI_SBOX_019"},
    };

    const auto path_between_first_fights = kh3ap::TreasureMappingResolver::infer_from_actor_name(
            L"g_kg_TreasureBox_C /Game/Levels/kg/kg_02/umap/kg_02_treasure.kg_02_treasure:PersistentLevel.02_g_kg_TreasureBox",
            treasure_lookup);
    assert(path_between_first_fights.has_value());
    assert(path_between_first_fights->source_id == L"KG_SBOX_001");
    assert(path_between_first_fights->bridge_key == L"TreasureKG:KG_SBOX_001");

    const auto switch_floor_maze = kh3ap::TreasureMappingResolver::infer_from_actor_name(
            L"g_kg_TreasureBox_C /Game/Levels/kg/kg_02/umap/kg_02_treasure.kg_02_treasure:PersistentLevel.03_g_kg_TreasureBox",
            treasure_lookup);
    assert(switch_floor_maze.has_value());
    assert(switch_floor_maze->source_id == L"KG_SBOX_002");
    assert(switch_floor_maze->bridge_key == L"TreasureKG:KG_SBOX_002");

    const auto path_between_second_fights = kh3ap::TreasureMappingResolver::infer_from_actor_name(
            L"g_kg_TreasureBox_C /Game/Levels/kg/kg_02/umap/kg_02_treasure.kg_02_treasure:PersistentLevel.04_g_kg_TreasureBox",
            treasure_lookup);
    assert(path_between_second_fights.has_value());
    assert(path_between_second_fights->source_id == L"KG_SBOX_003");
    assert(path_between_second_fights->bridge_key == L"TreasureKG:KG_SBOX_003");

    const auto monstropolis_elevator_post = kh3ap::TreasureMappingResolver::infer_from_actor_name(
            L"g_mi_TreasureBox_C /Game/Levels/mi/mi_04/umap/mi_04_treasure.mi_04_treasure:PersistentLevel.g_mi_TreasureBox_0",
            treasure_lookup);
    assert(monstropolis_elevator_post.has_value());
    assert(monstropolis_elevator_post->source_id == L"MI_SBOX_019");
    assert(monstropolis_elevator_post->bridge_key == L"TreasureMI:MI_SBOX_019");
    assert(monstropolis_elevator_post->mapping_reason == L"validated_actor_instance_map_monstropolis_mi04_actor_zero_to_sbox019");

    return 0;
}
