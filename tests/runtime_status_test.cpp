#include "kh3ap/bridge/session/runtime_status.h"
#include <cassert>
#include <iostream>

using namespace kh3ap::runtime_status;
int main()
{
    const auto camera = map_info("World /Game/Levels/bt_DLC/bt_08/umap/bt_08_camera_ms100.bt_08_camera_ms100");
    assert(camera["asset_path"].get_ref<const std::string&>() == "/Game/Levels/bt_DLC/bt_08/bt_08");
    assert(camera["object_path"].get_ref<const std::string&>() == "/Game/Levels/bt_DLC/bt_08/bt_08.bt_08");
    assert(map_info("World /Game/Levels/bt_DLC/bt_08/umap/bt_08_gameflow.bt_08_gameflow") == camera);
    const auto enemy = map_info("World /Game/Levels/tt/tt_01/umap/tt_01_enemy_06.tt_01_enemy_06:PersistentLevel");
    assert(enemy["room_id"].get_ref<const std::string&>() == "tt_01_enemy_06");
    assert(enemy["level_id"].get_ref<const std::string&>() == "tt_01");
    assert(enemy["object_name"].get_ref<const std::string&>() == "tt_01_enemy_06");
    assert(map_info("/Game/Other/tt_01").get_ref<const Json::object_t&>().empty());
    assert(map_info("/Game/Levels/tt").get_ref<const Json::object_t&>().empty());
    const auto goa = map_info("World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01");
    assert(goa["world_code"].get_ref<const std::string&>() == "GardenOfAssemblage" && goa["room_id"].get_ref<const std::string&>() == "GoA_01");

    Snapshot state;
    auto project = [&] { return state.project(10, "test", 1, 4, true); };
    Json context{{"world", "World /Game/Levels/ra/ra_01/ra_01.ra_01"},
        {"object", "Sora /Game/Levels/ra/ra_01/ra_01:Pawn @123 real=true"}, {"hook", "RuntimePlayablePawnContext"}};
    state.observe("runtime_context.json", context);
    state.observe("runtime_sora_state.json", Json{{"state_class", "TresCharStateMagic"}, {"current_state_id", 7},
        {"special_battle_mode_active", false}, {"source", "native"}, {"observed_at_utc", "now"}});
    state.observe("runtime_blocking_ui_state.json", Json{{"blocking_ui_active", false}, {"reason", "closed"}, {"callback_id", 2}});
    state.observe("runtime_item_grant_readiness.json", Json{{"schema", "kh3ap.item_delivery_readiness.v1"},
        {"status", "waiting"}, {"reason", "loading"}});
    const auto view = project();
    assert(view["presentation"]["sora_state"].get_ref<const std::string&>() == "TresCharStateMagic (id=7, native, now)");
    assert(view["presentation"]["blocking_ui"].get_ref<const std::string&>() == "false (closed, id=2)");
    assert(view["presentation"]["safe_context"].get_ref<const std::string&>() == "waiting: loading");
    assert(view["presentation"]["map_safe"].get_ref<const std::string&>() == "yes");
    assert(view["presentation"]["ap_items_grantable"].get_ref<const std::string&>() == "DLL");
    Json minimap{{"process_id", 10}, {"world", "/Game/Levels/ra/ra_01/ra_01.ra_01"},
        {"pawn", "Sora /Game/Levels/ra/ra_01/ra_01:Pawn"}, {"texture_path", "/Game/UI/Icon/Map/ra01_03.ra01_03"}, {"minimap_id", "ra01_03"}};
    state.observe("runtime_minimap_context.json", minimap);
    assert(project()["minimap"]["minimap_id"].get_ref<const std::string&>() == "ra01_03");
    for (const auto key : {"process_id", "world", "pawn", "texture_path"})
    {
        auto stale = minimap;
        stale[key] = key == std::string_view("process_id") ? Json(9) : Json("stale");
        state.observe("runtime_minimap_context.json", stale);
        assert(project()["minimap"].get_ref<const Json::object_t&>().empty());
    }
    state.observe("runtime_sora_state.json", Json{{"current_state", "legacy"}, {"objects", Json::array_t{
        Json{{"label", "state_comp"}, {"properties", "m_pCurrentState:ObjectProperty@24=LegacyState @99"}}}}});
    assert(project()["presentation"]["sora_state"].get_ref<const std::string&>() == "-");
    state.observe("runtime_map_ready.json", Json{{"world", "World /Game/Levels/kg/kg_01/kg_01_unioncross.kg_01_unioncross"}});
    assert(project()["presentation"]["map_safe"].get_ref<const std::string&>() == "no");
    assert(project()["minimap"].get_ref<const Json::object_t&>().empty());
    state.observe("runtime_context.json", Json{{"world", "/Game/Levels/xx/xx_01/xx_01"}});
    assert(project()["presentation"]["map_safe"].get_ref<const std::string&>() == "unknown");
    state.clear();
    assert(project()["map"].get_ref<const Json::object_t&>().empty());
    std::cout << "Native status interpretation and stale minimap tests passed\n";
}
