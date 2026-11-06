#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_marker_policy.h"
#include <cassert>
#include <set>
#include <string>
using namespace kh3ap::space;
int main()
{
    // Native list visibility matches the two independent seed options. Keep
    // all ordinary entries, including native exploration prerequisites.
    for (bool grindy : {false, true}) for (bool superbosses : {false, true}) {
        std::set<unsigned> hidden;
        for (unsigned id=0; id<46; ++id)
            if (!mission_list_enabled(id, grindy, superbosses)) hidden.insert(id);
        std::set<unsigned> expected;
        if (!grindy) expected.insert({20,21,22,27,33,34,35});
        if (!superbosses) expected.insert({13,17});
        if (!grindy || !superbosses) expected.insert(45);
        assert(hidden == expected);
    }
    for (unsigned id=0; id<46; ++id) assert(mission_row_id(std::to_wstring(id)) == id);
    for (auto row : {L"", L"46", L"99", L"100", L"-1", L"4a", L" 1", L"1_0"})
        assert(!mission_row_id(row));
    assert(!mission_list_enabled(46, true, true));
    // Registration uses the native array slot, including on the EGS front
    // path where the fourth argument is not initialized by the caller.
    constexpr uintptr_t guide=0x100000000ULL;
    for(int i=0;i<10;++i)assert(battle_marker_slot_index(guide,guide+808+i*88)==i);
    assert(battle_marker_slot_index(guide,guide+280)==-1); // world guide
    assert(battle_marker_slot_index(guide,guide+720)==-1); // navigation marker
    assert(battle_marker_slot_index(guide,guide+809)==-1); // misaligned
    assert(battle_marker_slot_index(guide,guide+808+10*88)==-1);
    assert(battle_marker_slot_index(guide,guide-1)==-1);
    assert(camp_returns_to_space(L"World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01"));
    assert(!camp_returns_to_space(L"World /Game/Levels/tt/tt_01/tt_01.tt_01"));
    assert(!camp_returns_to_space(L"World /Game/Levels/wm/wm_01/wm_01.wm_01"));
    assert(!camp_returns_to_space(L"World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01_preview"));
    assert(!camp_returns_to_space(L""));
    constexpr auto flight = L"World /Game/Levels/gm/gm_01/gm_01.gm_01";
    constexpr auto astro = L"/Game/Levels/gm/gm_01/umap/Battle/gm_01_battle_10.gm_01_battle_10:PersistentLevel.TresGumiShipBoss619BattleStarter2";
    assert(restore_story_encounter(true, true, flight, astro));
    assert(!restore_story_encounter(false, true, flight, astro));
    assert(!restore_story_encounter(true, false, flight, astro));
    assert(!restore_story_encounter(true, true, L"World /Game/Levels/wm/wm_01/wm_01.wm_01", astro));
    assert(!restore_story_encounter(true, true, flight, L"/Game/Levels/gm/gm_01/umap/Battle/gm_01_battle_10.Default__Starter"));
    assert(!restore_story_encounter(true, true, flight, L"/Game/Levels/gm/gm_03/umap/Battle/gm_03_battle_06.gm_03_battle_06:PersistentLevel.Omega"));
    assert(!restore_story_encounter(true, true, flight, L"/Game/Levels/gm/gm_01/umap/Battle/gm_01_battle_11.gm_01_battle_11:PersistentLevel.Starter"));
    // A deferred/failed Garden evaluation must never retain retail first-visit
    // or visited state. Once loaded, the same destination becomes selectable
    // using Garden progress, without changing any saved story flag.
    for (int previous : {0, 1, 2, 3, 4}) {
        const int waiting=landing_world_state(true,std::nullopt,previous);
        assert(waiting==0);
        assert(landing_world_state(true,true,waiting)==3);
        assert(landing_world_state(true,false,waiting)==1);
        assert(landing_world_state(false,true,previous)==0);
    }
    assert(landing_world_state(true,true,4)==4);
    assert(landing_world_state(true,false,4)==1);
    assert(supported_reward_count(79));
    assert(supported_reward_count(85));
    assert(supported_reward_count(86));
    assert(supported_reward_count(93));
    for (size_t count : {0u, 78u, 80u, 81u, 84u, 87u, 92u, 94u, 99u, 100u}) assert(!supported_reward_count(count));
    assert(battle_first_reward_matches(L"gm_02_battle_10",L"gm_02_Battle_10_1"));
    assert(battle_first_reward_matches(L"gm_01_Battle_01",L"gm_01_Battle_01_1"));
    assert(battle_first_reward_matches(L"GM_03_BATTLE_06",L"gm_03_Battle_06_1"));
    assert(!battle_first_reward_matches(L"gm_02_battle_10",L"gm_02_Battle_11_1"));
    assert(!battle_first_reward_matches(L"gm_02_battle_10",L"gm_01_Battle_10_1"));
    assert(!battle_first_reward_matches(L"gm_02_battle_10",L"gm_02_Battle_10_2"));
    assert(!battle_first_reward_matches(L"gm_02_battle_1",L"gm_02_Battle_01_1"));
    assert(!battle_first_reward_matches(L"",L"_1"));
    assert(battle_from_starter_path(L"/Game/Levels/gm/gm_02/umap/Battle/gm_02_battle_04.gm_02_Battle_04:PersistentLevel.Starter")==L"gm_02_battle_04");
    assert(battle_from_starter_path(L"/Game/Levels/gm/gm_03/umap/Battle/gm_03_battle_06.gm_03_Battle_06:PersistentLevel.Boss")==L"gm_03_battle_06");
    assert(battle_from_starter_path(L"/Game/Levels/gm/gm_02/gm_02.gm_02:PersistentLevel.Starter").empty());
    assert(battle_from_starter_path(L"/Game/Levels/tt/umap/Battle/gm_02_battle_04.X:PersistentLevel.Starter").empty());
    assert(battle_from_starter_path(L"/Game/Levels/gm/gm_02/umap/Battle/gm_02_battle_04").empty());
    assert(inventory_delivery_world(L"World /Game/Levels/wm/wm_01/wm_01.wm_01"));
    assert(inventory_delivery_world(L"World /Game/Levels/gm/gm_03/gm_03.gm_03"));
    assert(!inventory_delivery_world(L"World /Game/Levels/gm/gm_04/gm_04.gm_04"));
    assert(!inventory_delivery_world(L"World /Game/Levels/sf/sf_50/sf_50.sf_50"));
    assert(supports_inventory_grant(L"bp_tres_get_item", L"ITEM_POTION"));
    for (auto token : {L"KEY_ITEM06", L"KEY_ITEM07", L"KEY_ITEM08", L"KEY_ITEM09", L"KEY_ITEM10"})
        assert(!supports_inventory_grant(L"bp_tres_get_item", token));
    assert(supports_inventory_grant(L"bp_tres_get_item", L"KEY_ITEM11"));
    assert(supports_inventory_grant(L"bp_tres_get_item_add_munny"));
    assert(supports_inventory_grant(L"ultima_weapon_shard"));
    for (auto method : {
                       L"durable_sora_ability", L"durable_sora_vbonus", L"party_member_unlock",
                       L"keyblade_style", L"progressive_keyblade_level", L"controller_unlock",
                       L"bp_tres_start_game_over", L"direct_player_bad_status", L"unknown"})
        assert(!supports_inventory_grant(method));
    assert(inventory_ui_allowed(false, L"", L""));
    assert(inventory_ui_allowed(true, L"pause_menu", L"TresUIP_MenuGummiShip"));
    assert(!inventory_ui_allowed(true, L"cutscene", L"TresUIP_MenuGummiShip"));
    for (auto panel : {L"TresUIP_CampMenu", L"TresUIP_SaveLoad", L"TresUIP_GameHelp",
                       L"TresUIP_EditGummiShip", L"TresUIP_MenuGummiShipBackground", L""})
        assert(!inventory_ui_allowed(true, L"pause_menu", panel));
    assert(is_entry_scene(L"World /Game/Levels/rg/rg_51/rg_51.rg_51"));
    assert(!is_entry_scene(L"World /Game/Levels/rg/rg_01/rg_01.rg_01"));
    assert(entry_scene_transition(true, world_map, entry_scene));
    assert(entry_scene_transition(true, entry_scene, tower_scene));
    assert(!entry_scene_transition(false, entry_scene, tower_scene));
    assert(!entry_scene_transition(true, world_map, tower_scene));
    assert(!entry_scene_transition(true, entry_scene, L"/Game/Levels/rg/rg_01/rg_01"));
    for (unsigned id=0; id<9; ++id) {
        assert(unlock_travel_point(4, id, travel_maps[id/3], travel_tags[id]));
        assert(!unlock_travel_point(3, id, travel_maps[id/3], travel_tags[id]));
        assert(!unlock_travel_point(5, id, travel_maps[id/3], travel_tags[id]));
        assert(!unlock_travel_point(4, id, L"/Game/Levels/he/he_01/he_01", travel_tags[id]));
        assert(!unlock_travel_point(4, id, travel_maps[id/3], L"gm_01_WorldSymbol_HE"));
    }
    for (unsigned id=9; id<256; ++id)
        assert(!unlock_travel_point(4, id, travel_maps[0], travel_tags[0]));
    for (unsigned id=0; id<9; ++id) {
        assert(world_symbol_code(3,id)==world_symbol_codes[id]);
        for (unsigned kind : {4u,5u,7u,8u,10u}) assert(world_symbol_code(kind,id).empty());
    }
    assert(world_symbol_code(3,4)==L"fz");
    assert(world_symbol_code(3,5)==L"mi");
    assert(overview_default_fallback(0,3,0,false)==15); // New-game Olympus -> first Starlight travel point.
    assert(overview_default_fallback(1,3,4,false)==18);
    assert(overview_default_fallback(2,3,8,false)==21);
    for (unsigned id=0; id<9; ++id) {
        const unsigned region=id<4?0:id<8?1:2;
        assert(overview_default_fallback(region,3,id,true)==-1); // Owned remembered world.
        assert(overview_default_fallback(region,3,id,false)==static_cast<int>(15+3*region));
        assert(overview_default_fallback((region+1)%3,3,id,true)>=15); // Wrong region.
    }
    for (unsigned region=0; region<3; ++region)
        for (unsigned id=0; id<256; ++id)
            for (unsigned kind : {4u,5u,6u,7u,8u,10u})
                assert(overview_default_fallback(region,kind,id,false)==-1);
    assert(overview_default_fallback(3,3,0,false)==-1);
    assert(overview_default_fallback(0,3,99,false)==-1);
    // Regression: native WM_FirstMapStartData starts BX beside locked MI.
    // A first embark must resolve the SELECTED world, including all other
    // vanilla cross-world fallbacks, without opening those other worlds.
    const std::pair<int, std::wstring_view> first_starts[]{
        {4,L"gm_01_WorldSymbol_HE"}, {12,L"gm_01_WorldSymbol_TT"},
        {5,L"gm_01_WorldSymbol_TS"}, {7,L"gm_01_WorldSymbol_RA"},
        {8,L"gm_02_WorldSymbol_FZ"}, {11,L"gm_02_WorldSymbol_MI"},
        {19,L"gm_02_WorldSymbol_BX"}, {9,L"gm_02_WorldSymbol_CA"},
        {14,L"gm_03_WorldSymbol_KG"}};
    for (const auto& [world, expected] : first_starts) {
        const auto route=first_embark_destination(world);
        assert(route.tag==expected);
        assert(world_embark_code(route.map,route.tag)==route.code);
        assert(world_symbol_code(3,route.symbol_id)==route.code);
    }
    for (int world : {-1,0,3,23,24,99}) assert(first_embark_destination(world).code.empty());
    // Embarking beside a world needs the same Keyblade as landing in it.
    // Ordinary travel points and other space starts stay independent.
    for (size_t i=0;i<world_embark_tags.size();++i) {
        const auto region=i<4?0:i<8?1:2;
        assert(world_embark_code(travel_maps[region],world_embark_tags[i])==world_symbol_codes[i]);
        assert(world_embark_code(travel_maps[(region+1)%3],world_embark_tags[i]).empty());
    }
    for (size_t i=0;i<travel_tags.size();++i)
        assert(world_embark_code(travel_maps[i/3],travel_tags[i]).empty());
    assert(world_embark_code(travel_maps[0],L"gm_01_start01").empty());
    assert(world_embark_code(travel_maps[0],L"gm_01_WorldSymbol_TT_extra").empty());
    for (unsigned id=9; id<256; ++id) assert(world_symbol_code(3,id).empty());
    assert(redirect_to_garden(L"World /Game/Levels/kg/kg_01/kg_01.kg_01", world_map));
    assert(redirect_to_garden(L"World /Game/Levels/tt/tt_01/tt_01.tt_01", world_map));
    assert(!redirect_to_garden(L"World /Game/Levels/gm/gm_01/gm_01.gm_01", world_map));
    assert(!redirect_to_garden(L"World /Game/Levels/wm/wm_01/wm_01.wm_01", world_map));
    assert(redirect_to_garden(L"/Game/Levels/ts/ts_01/ts_01", L"/Game/Levels/gm/gm_01/gm_01"));
    assert(!redirect_to_garden(L"/Game/Levels/gm/gm_01/gm_01", L"/Game/Levels/gm/gm_02/gm_02"));
    assert(!redirect_to_garden(garden, world_map));
    assert(!redirect_to_garden(L"", world_map));
    assert(!redirect_to_garden(L"/Game/Levels/kg/kg_01/kg_01", garden));
    // The stock Return to Garden save-point action still requests WM01.
    // Both the native jump and checkpoint hooks share this redirect policy.
    for (auto source : {
             L"World /Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown",
             L"World /Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin",
             L"World /Game/Maps/APKH02Mirrors/APKH02Stairs.APKH02Stairs",
             L"World /Game/Maps/APKH02Mirrors/APKH02Corridor.APKH02Corridor",
             L"World /Game/Maps/APKH02Mirrors/APKH02Tunnel.APKH02Tunnel",
             L"World /Game/Maps/APKH02Mirrors/APKH02Phantom.APKH02Phantom",
             L"World /Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest",
             L"World /Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths",
             L"World /Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands",
             L"World /Game/Maps/APKH02Mirrors/APKH02Transition.APKH02Transition",
             L"World /Game/Maps/APKH02Mirrors/APKH02Epilogue.APKH02Epilogue",
             L"World /Game/Maps/APKH02Mirrors/APKH02MirrorChallenge.APKH02MirrorChallenge"})
    {
        for (bool journey : {false, true})
        {
            assert(redirect_to_garden(source, world_map, journey));
            assert(redirect_to_garden(source, travel_maps[0], journey));
            assert(!redirect_to_garden(source, garden, journey));
            assert(!redirect_to_garden(source, L"/Game/Maps/APKH02Mirrors/APKH02Forest", journey));
        }
    }
    for (auto source : {
             L"World /Game/Maps/APKH02CastleTown/Other.Other",
             L"World /Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest:PersistentLevel.SavePoint",
             L"World /Game/Maps/APKH02Mirrors/APKH02ForestExtra.APKH02ForestExtra",
             L"World /Game/Maps/StationOfAwakening/umap/StationOfAwakening.StationOfAwakening",
             L"World /Game/Maps/TresGameTitle.TresGameTitle"})
    {
        assert(!redirect_to_garden(source, world_map));
    }
    // Post-Davy Jones must not enter the forest map whose scenes startup has
    // completed. The same policy serves native jumps and saved checkpoints.
    for (bool journey : {false, true}) {
        assert(redirect_to_garden(L"World /Game/Levels/ca/ca_03/ca_03.CA_03", entry_scene, journey));
        assert(!redirect_to_garden(world_map, entry_scene, journey));
        assert(!redirect_to_garden(travel_maps[0], entry_scene, journey));
        assert(!redirect_to_garden(garden, entry_scene, journey));
        assert(!redirect_to_garden(L"", entry_scene, journey));
        assert(!redirect_to_garden(L"/Game/Levels/ca/ca_03/ca_03", L"/Game/Levels/sf/sf_50/sf_50_extra", journey));
        assert(!redirect_to_garden(L"/Game/Levels/ca/ca_03/ca_03", L"/Game/Levels/dw/dw_21/dw_21", journey));
    }
    assert(!redirect_to_garden(entry_scene, world_map, true));
    assert(!redirect_to_garden(tower_scene, world_map, true));
    assert(!redirect_to_garden(tower_scene, entry_scene, true));
    assert(redirect_to_garden(entry_scene, world_map, false));
    // Toy Box and Corona both select GM50 when the other world is unfinished;
    // Monstropolis uses its gm431 start. Dark World and Frozen's YT50
    // continuation also have GM50 interludes.
    // These are hub returns even during a previously armed space journey.
    for (bool journey : {false, true}) {
        for (auto source : {
                 L"World /Game/Levels/ts/ts_02/ts_02.ts_02",
                 L"World /Game/Levels/ra/ra_01/ra_01.ra_01",
                 L"World /Game/Levels/mi/mi_01/mi_01.mi_01",
                 L"World /Game/Levels/dw/dw_21/dw_21.dw_21",
                 L"World /Game/Levels/yt/yt_50/yt_50.yt_50"}) {
            assert(redirect_to_garden(source, gummi_scene, journey));
            assert(!redirect_to_garden(source, L"/Game/Levels/gm/gm_50/gm_50_extra", journey));
        }
        for (auto source : {world_map, travel_maps[0], travel_maps[1], travel_maps[2], garden, std::wstring_view{}})
        {
            assert(!redirect_to_garden(source, gummi_scene, journey));
            assert(!redirect_to_garden(source, yen_sid_scene, journey));
        }
        // Frozen's cleared-Monstropolis branch must not enter YT50 and run
        // its vanilla Dream Heartbinder grant, including with initial YT flags.
        assert(redirect_to_garden(L"World /Game/Levels/fz/fz_05/fz_05.fz_05", yen_sid_scene, journey));
        assert(redirect_to_garden(L"World /Game/Levels/dw/dw_21/dw_21.dw_21", yen_sid_scene, journey));
        assert(!redirect_to_garden(L"World /Game/Levels/fz/fz_05/fz_05.fz_05", L"/Game/Levels/yt/yt_50/yt_50_extra", journey));
    }
    // Preserve the audited non-Gummi story destinations and existing returns.
    constexpr auto frozen=L"World /Game/Levels/fz/fz_05/fz_05.fz_05";
    assert(redirect_to_garden(frozen, world_map)); // ending's hub checkpoint
    assert(!redirect_to_garden(frozen, L"/Game/Levels/dw/dw_21/dw_21"));
    assert(redirect_to_garden(frozen, yen_sid_scene));
    assert(!redirect_to_garden(L"World /Game/Levels/he/he_01/he_01.he_01", L"/Game/Levels/dw/dw_71/dw_71"));
    assert(!redirect_to_garden(L"World /Game/Levels/dw/dw_21/dw_21.dw_21", L"/Game/Levels/di/di_50/di_50"));
    assert(redirect_to_garden(L"World /Game/Levels/bx/bx_02/bx_02.bx_02", entry_scene));
    assert(redirect_to_garden(L"World /Game/Levels/tt/tt_01/tt_01.tt_01", world_map));
    assert(!redirect_to_garden(L"World /Game/Levels/tt/tt_01/tt_01.tt_01", L"/Game/Levels/tt/tt_50/tt_50"));
    assert(destination_code(L"/Game/Levels/ts/ts_01/ts_01") == L"ts");
    assert(destination_code(garden).empty());
    // Bistro's TT package prefix must not require Shooting Star or open TT.
    assert(!destination_requires_keyblade(L"/Game/Levels/tt/tt_40/tt_40"));
    assert(destination_requires_keyblade(L"/Game/Levels/tt/tt_01/tt_01"));
    assert(destination_requires_keyblade(L"/Game/Levels/tt/tt_40/tt_40_extra"));
    assert(destination_requires_keyblade(L"/Game/Levels/tt/tt_41/tt_41"));
    assert(destination_requires_keyblade(L"/Game/Levels/bx/bx_01/bx_01"));
    assert(!destination_requires_keyblade(garden));
    assert(!destination_requires_keyblade(world_map));
    for (const auto map : travel_maps) assert(!destination_requires_keyblade(map));
    for (unsigned id=0; id<12; ++id) {
        for (unsigned region=0; region<3; ++region) {
            const auto world = L"World " + std::wstring{travel_maps[region]} +
                L".gm_0" + std::to_wstring(region+1);
            const bool expected=(id==3 && region==1)||(id==6 && region==2);
            assert(rearm_initial_travel_point(true,id,world,false)==expected);
            assert(!rearm_initial_travel_point(true,id,world,true));
            assert(!rearm_initial_travel_point(false,id,world,false));
        }
        assert(!rearm_initial_travel_point(true,id,world_map,false));
        assert(!rearm_initial_travel_point(true,id,garden,false));
    }
    std::set<std::wstring> items;
    auto has = [&](std::wstring_view name) { return items.contains(std::wstring{name}); };
    items={L"Nano Gear"};
    const auto bx=first_embark_destination(19);
    assert(landing_allowed(world_embark_code(bx.map,bx.tag),has));
    assert(!landing_allowed(world_embark_code(bx.map,L"gm_02_WorldSymbol_MI"),has));
    // Each Keyblade alone opens its world, including the former movement,
    // magic, and three-other-Keyblade gates. Other worlds remain closed.
    for (const auto& owned : landings) {
        items = {std::wstring{owned.keyblade}};
        for (const auto& destination : landings)
            assert(landing_allowed(destination.code, has) == (destination.code == owned.code));
        for (unsigned id=0; id<9; ++id)
            assert(landing_allowed(world_symbol_code(3,id), has) == (world_symbol_codes[id] == owned.code));
        for (size_t i=0;i<world_embark_tags.size();++i)
            assert(landing_allowed(world_embark_code(travel_maps[i<4?0:i<8?1:2],world_embark_tags[i]),has)==(world_symbol_codes[i]==owned.code));
    }
    // Combat items never substitute for a world's Keyblade.
    items = {L"Ability: Block", L"Ability: Air Slide", L"Ability: Dodge Roll",
             L"Ability: Glide", L"Magic: Blizzard", L"Unlock: Magic"};
    for (const auto& destination : landings) assert(!landing_allowed(destination.code, has));
    items.clear();
    for (const auto& destination : landings) assert(!landing_allowed(destination.code, has));
    for (const auto& destination : landings) items.insert(std::wstring{destination.keyblade});
    for (const auto& destination : landings) assert(landing_allowed(destination.code, has));
    assert(!landing_allowed(L"bt", has));
    assert(!landing_allowed(L"unknown", has));
}
