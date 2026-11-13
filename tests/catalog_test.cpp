#include "kh3ap/bridge/catalog/catalog.h"
#include "kh3ap/features/kh02/progress/kh02_check_policy.h"
#include "kh3ap/bridge/catalog/catalog_data.h"
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/catalog/location_catalog_data.h"
#define NOMINMAX
#include <Windows.h>
#include <cassert>
#include <fstream>
#include <iostream>

using Json = kh3ap::json::Value;
using namespace kh3ap;
auto parse(std::string_view text) -> Json { Json j; assert(kh3ap::json::parse(j, text)); return j; }
auto fails(const auto& fn) -> bool { try { fn(); } catch (const std::exception&) { return true; } return false; }
int main(int argc, char** argv)
{
    const auto aqua_finish = kh02_checks::condition(Json{
        {"source_table", "KH02"}, {"source_id", "Boss:UnlockAqua"},
        {"type", "character_unlock"}, {"kh02_flag", "BattleBonus_DW_dw_10_DevilWave"}});
    assert(aqua_finish.has_value());
    assert(!kh02_checks::earned(Json{{"flags", {{"BattleBonus_DW_dw_09_DevilTower", 1}}}}, *aqua_finish));
    assert(kh02_checks::earned(Json{{"flags", {{"BattleBonus_DW_dw_10_DevilWave", 1}}}}, *aqua_finish));
    assert(native_catalog::runtime_token_for_item(1263022607) == L"KH3AP_PARTY_MEMBER_UNLOCK::PartyMember_003");
    assert(native_catalog::runtime_token_for_item(-1).empty());
    assert(native_catalog::runtime_token_for_item(0).empty());
    if (argc == 2)
    {
        const auto document = kh3ap::json::read_seed_document(argv[1]);
        std::ofstream output(std::string(argv[1]) + ".expanded.json");
        output << document->text;
        return 0;
    }
    const auto locations = parse(kh3ap::json::location_catalog_data);
    const auto first = locations["locations"].begin();
    const auto id = std::stoll(first.key());
    Json compact{{"kh3_seed", {{"seed_name", "compact"}, {"slot", 1}}}, {"bridge", Json::object()},
        {"location_catalog", {{"version", 1}, {"sha256", locations["sha256"]},
            {"assignments", Json::array({Json{{"assigned_item_name", "Foreign reward"}, {"assigned_item_player", 2}}})},
            {"entries", Json::array({Json::array({id, 0, Json{{"mom_song_id", 12}, {"shop_price", 99}}, Json::array({"bridge_key"})})})}}}};
    const auto restored = kh3ap::json::expand_slot_data(compact);
    auto expected = first.value();
    expected.erase("bridge_key"); expected["assigned_item_name"] = "Foreign reward";
    expected["assigned_item_player"] = 2; expected["mom_song_id"] = 12; expected["shop_price"] = 99;
    assert(restored["bridge"]["locations"] == Json::array({expected}));
    assert(!restored.contains("location_catalog"));
    auto bad = compact; bad["location_catalog"]["sha256"] = "wrong";
    assert(fails([&] { kh3ap::json::expand_slot_data(bad); }));
    bad = compact; bad["location_catalog"]["entries"].push_back(bad["location_catalog"]["entries"][0]);
    assert(fails([&] { kh3ap::json::expand_slot_data(bad); }));
    bad = compact; bad["location_catalog"]["entries"][0][1] = 3;
    assert(fails([&] { kh3ap::json::expand_slot_data(bad); }));
    bad = compact; bad["location_catalog"]["entries"][0][2]["ap_location_id"] = id + 1;
    assert(fails([&] { kh3ap::json::expand_slot_data(bad); }));
    bad = compact; bad["location_catalog"]["entries"][0][3].push_back("missing_field");
    assert(fails([&] { kh3ap::json::expand_slot_data(bad); }));
    const auto directory = std::filesystem::temp_directory_path() / ("kh3-compact-catalog-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "slot_data.json";
    { std::ofstream file(path); file << compact.dump(); }
    const auto document = kh3ap::json::read_seed_document(path);
    assert(document->value == restored && parse(document->text) == restored);
    assert(kh3ap::json::read_seed_document(path) == document);
    { std::ofstream file(path); file << bad.dump(); }
    assert(fails([&] { kh3ap::json::read_seed_document(path); }));
    std::filesystem::remove(path); std::filesystem::remove(directory);
    auto seed = parse(R"({"kh3_seed":{"seed_name":"catalog","slot":1},"ultima_weapon_shard_count":3,"bridge":{"locations":[
        {"ap_location_id":100,"bridge_key":"VBonus:Vbonus_016","source_table":"VBonus","source_id":"Vbonus_016","bridge_delivery_required":true},
        {"ap_location_id":101,"bridge_key":"LevelUp:4","source_table":"LevelUp","source_id":"4"},
        {"ap_location_id":102,"bridge_key":"TreasureTT:TT1","type":"treasure","source_table":"TreasureTT","source_id":"TT1"},
        {"ap_location_id":103,"bridge_key":"Always:1","world":"Always On","source_id":"ON"},
        {"source_table":"Event","source_id":"EVENT_KEYBLADE_002"},
        {"source_table":"Event","source_id":"EVENT_007"},
        {"source_table":"Event","source_id":"TresUIMobilePortalDataAsset"},
        {"source_id":"MusicChest","music_chest_placement_version":1}
    ]}})");
    seed["runtime_reward_bundles"] = parse(R"([
        {"bridge_key":"VBonus:Vbonus_016","randomized_tokens":[{"token":"ETresAbilityKind::AIR_RECOVERY"},{"token":"ITEM_POTION"},{"token":"ETresAbilityKind::AIRSLIDE"},{"token":"ETresAbilityKind::AIRSLIDE"},{"token":"ETresAbilityKind::NONE"}]},
        {"bridge_key":"LevelUp:4","randomized_tokens":["ITEM_POTION","ETresAbilityKind::AIRSLIDE"]}])");
    native_catalog::Seed catalog; catalog.configure(seed);
    int character_count = 0;
    const auto shared_catalog = parse(native_catalog::shared_data);
    for (const auto& [key, item] : shared_catalog["items"].items())
    {
        if (!item.value("token", "").starts_with("KH3AP_CHARACTER_UNLOCK::"))
        {
            continue;
        }
        const auto unlock = catalog.decode(Json{{"index", 0}, {"item_id", std::stoll(key)},
            {"location_id", 102}, {"from_player", 1}});
        assert(unlock.supported && unlock.local && unlock.bridge_required && !unlock.inventory);
        assert(unlock.primary.grant.grant_method == L"character_unlock");
        assert(unlock.primary.grant.action_kind == L"grant_character_unlock");
        ++character_count;
    }
    assert(character_count == 4);
    auto receipt = Json{{"index", 0}, {"item_id", 1263022199}, {"location_id", 100}, {"from_player", 1},
        {"from_player_name", "Sora"}, {"location_name", "Bonus"}, {"kh3_grant_method", "fake"}, {"supported", false}, {"bridge_key", "forged"}};
    auto decoded = catalog.decode(receipt);
    assert(decoded.supported && decoded.local && decoded.bridge_required && !decoded.inventory);
    assert(decoded.bridge_key == "VBonus:Vbonus_016" && decoded.primary.grant.grant_method == L"durable_sora_ability");
    assert(!decoded.primary.flag_rows.empty() && decoded.rewards.size() == 3);
    const auto expanded = item_delivery::expand_catalog({{0, decoded}}).at(0);
    assert(expanded.size() == 4 && expanded[2].runtime_flag_name != expanded[3].runtime_flag_name);
    const auto description = native_catalog::describe(receipt, decoded, expanded);
    assert(description["native_supported"].get<bool>());
    assert(description["kh3_grant_method"].get_ref<const std::string&>() == "durable_sora_ability");
    assert(description["kh3_secondary_rewards"].get_ref<const Json::array_t&>().size() == 3);
    assert(description["native_secondary_rewards"].get_ref<const Json::array_t&>()[1]["kh3_runtime_flag_name"].get_ref<const std::string&>() ==
        std::string(expanded[2].runtime_flag_name.begin(), expanded[2].runtime_flag_name.end()));
    assert(description["native_secondary_rewards"].get_ref<const Json::array_t&>()[2]["kh3_runtime_flag_name"].get_ref<const std::string&>() ==
        std::string(expanded[3].runtime_flag_name.begin(), expanded[3].runtime_flag_name.end()));
    assert(description["bridge_key"].get_ref<const std::string&>() == "VBonus:Vbonus_016");
    receipt["from_player"] = 2;
    decoded = catalog.decode(receipt);
    assert(!decoded.local && decoded.bridge_key.empty() && decoded.rewards.empty()); // Foreign ID collisions cannot borrow local bundles.
    const auto foreign_description = native_catalog::describe(receipt, decoded, item_delivery::expand_catalog({{0, decoded}}).at(0));
    assert(foreign_description["bridge_key"].is_null() && !foreign_description.contains("kh3_secondary_rewards"));
    receipt["from_player"] = 1; receipt["location_id"] = 101;
    decoded = catalog.decode(receipt);
    assert(decoded.level_up && item_delivery::expand_catalog({{0, decoded}}).at(0).size() == 1);
    receipt["location_id"] = 102; receipt["item_id"] = 1263022503;
    decoded = catalog.decode(receipt); assert(decoded.local && decoded.inventory && !decoded.bridge_required);
    receipt["item_id"] = 1263022577;
    decoded = catalog.decode(receipt);
    assert(decoded.bridge_required && decoded.primary.progressive_base == L"KH3AP_PROGRESSIVE_KEYBLADE_LEVEL");
    assert(item_delivery::expand_catalog({{0, decoded}, {2, decoded}}).at(2).front().runtime_token.ends_with(L"::2"));
    assert(native_catalog::describe(receipt, decoded, item_delivery::expand_catalog({{0, decoded}, {2, decoded}}).at(2))
        ["kh3_runtime_token"].get_ref<const std::string&>().ends_with("::2"));
    receipt["item_id"] = 1263022599;
    decoded = catalog.decode(receipt); assert(decoded.primary.shard_required == 3);
    assert(item_delivery::expand_catalog({{0, decoded}}).at(0).front().runtime_token == L"KH3AP_ULTIMA_WEAPON_SHARD::1::3");
    assert(native_catalog::describe(receipt, decoded, item_delivery::expand_catalog({{0, decoded}}).at(0))
        ["kh3_runtime_token"].get_ref<const std::string&>() == "KH3AP_ULTIMA_WEAPON_SHARD::1::3");
    receipt["item_id"] = 1263022596;
    assert(catalog.decode(receipt).primary.grant.grant_method == L"direct_player_bad_status");
    receipt["item_id"] = 1263022658;
    decoded = catalog.decode(receipt);
    assert(decoded.supported && decoded.bridge_required);
    assert(decoded.primary.grant.grant_method == L"king_mickey_trap");
    assert(decoded.primary.grant.runtime_token == L"KH3AP_TRAP::KING_MICKEY");
    assert(native_catalog::describe(receipt, decoded, item_delivery::expand_catalog({{0, decoded}}).at(0))
        ["action"]["kind"] == "trigger_trap");
    receipt["item_id"] = 1263022660;
    decoded = catalog.decode(receipt);
    assert(decoded.supported && decoded.bridge_required);
    assert(decoded.primary.grant.grant_method == L"betrayal_trap");
    assert(decoded.primary.grant.runtime_token == L"KH3AP_TRAP::BETRAYAL");
    assert(native_catalog::describe(receipt, decoded, item_delivery::expand_catalog({{0, decoded}}).at(0))
        ["action"]["kind"] == "trigger_trap");
    receipt["location_id"] = 103;
    assert(catalog.decode(receipt).always_on);
    receipt["item_id"] = 999;
    assert(!catalog.decode(receipt).supported); // Item names and client hints cannot supply unknown grants.
    receipt["item_id"] = 1.5;
    assert(fails([&] { catalog.decode(receipt); }));
    receipt["location_id"] = -1; receipt["from_player"] = 2;
    for (const auto& [id, method] : std::map<int64_t, std::wstring>{
        {1263022597, L"bp_tres_get_item_add_munny"}, {1263022598, L"bp_tres_get_item_add_munny"},
        {1263022573, L"controller_unlock"}, {1263022449, L"durable_sora_vbonus"},
        {1263022345, L"durable_sora_vbonus"}, {1263022607, L"party_member_unlock"},
        {1263022436, L"bp_tres_get_item"}, {1263022421, L"bp_tres_get_item"}})
    {
        receipt["item_id"] = id;
        decoded = catalog.decode(receipt);
        assert(decoded.supported && decoded.primary.grant.grant_method == method);
        const auto detail = native_catalog::describe(receipt, decoded, item_delivery::expand_catalog({{0, decoded}}).at(0));
        assert(detail["kh3_grant_method"].get_ref<const std::string&>() == std::string(method.begin(), method.end()));
        assert(detail["action"]["kind"].get_ref<const std::string&>() == std::string(decoded.primary.grant.action_kind.begin(), decoded.primary.grant.action_kind.end()));
        if (id == 1263022573) assert(detail["action"].contains("unlock_display_name"));
        if (id == 1263022597) assert(detail["action"]["munny_amount"].get<double>() == 100);
        if (id == 1263022597) assert(decoded.primary.grant.munny_delta == 100);
        if (id == 1263022598) assert(decoded.primary.grant.munny_delta == 1000);
        if (id == 1263022436) assert(decoded.primary.grant.action_kind == L"grant_keyblade" && !decoded.inventory);
        if (id == 1263022449) assert(decoded.primary.grant.action_kind == L"grant_magic" && !decoded.primary.flag_rows.empty());
    }

    // Native default party forms have no AP item identity or grant route.
    for (const int64_t id : {1263022605, 1263022606, 1263022625, 1263022626,
                            1263022627, 1263022628, 1263022629, 1263022630})
    {
        receipt["item_id"] = id;
        assert(!catalog.decode(receipt).supported);
    }

    // A large ordinary receipt history remains comfortably within the 8 MiB
    // IPC packet limit, including JSON escaping in the snapshot envelope.
    receipt["item_id"] = 1263022503;
    Json::object_t descriptions;
    for (int i = 0; i < 4000; ++i)
    {
        receipt["index"] = i;
        decoded = catalog.decode(receipt);
        descriptions.emplace(std::to_string(i), native_catalog::describe(receipt, decoded,
            item_delivery::expand_catalog({{i, decoded}}).at(i)));
    }
    std::string description_body, packet;
    kh3ap::json::write(Json(std::move(descriptions)), description_body);
    kh3ap::json::write(Json{{"channel", "runtime_item_descriptions.json"}, {"payload", description_body}}, packet);
    assert(packet.size() < 8 * 1024 * 1024);

    // Exercise the complete generated item-ID catalog through the DLL decoder.
    // Python runtime description tests no longer own a second decoder.
    const auto registry = parse(native_catalog::shared_data);
    size_t supported_items{};
    for (const auto& [id, entry] : registry["items"].get_ref<const Json::object_t&>())
    {
        receipt["item_id"] = std::stoll(id);
        decoded = catalog.decode(receipt);
        const auto token = entry["token"].get_ref<const std::string&>();
        if (token.empty()) { assert(!decoded.supported); continue; }
        assert(decoded.supported);
        const auto grants = item_delivery::expand_catalog({{0, decoded}}).at(0);
        const auto detail = native_catalog::describe(receipt, decoded, grants);
        assert(detail["native_supported"].get<bool>() && !detail["kh3_grant_method"].get_ref<const std::string&>().empty());
        assert(!detail["action"]["kind"].get_ref<const std::string&>().empty());
        assert(detail["kh3_runtime_token"].get_ref<const std::string&>() == std::string(grants.front().runtime_token.begin(), grants.front().runtime_token.end()));
        ++supported_items;
    }
    assert(supported_items > 400);

    std::string text; kh3ap::json::write(seed, text);
    const auto events = native_catalog::check_setup(text, native_catalog::CheckKind::Event);
    assert(events.seed_name == L"catalog" && events.slot == 1 && events.rows.size() == 2);
    assert(events.rows[0].requires_tt_clear && events.rows[1].threshold == 600);
    const auto bonuses = native_catalog::check_setup(text, native_catalog::CheckKind::VBonus);
    assert(bonuses.rows.size() == 1 && bonuses.rows[0].flag_name == L"gameflow_VBONUS_016_tt_003");
    assert(native_catalog::check_setup(text, native_catalog::CheckKind::Chest).rows.size() == 2);
    assert(native_catalog::check_setup(R"({"kh3_seed":{"seed_name":"empty","slot":1}})", native_catalog::CheckKind::VBonus).rows.empty());
    assert(fails([] { native_catalog::check_setup(R"({"kh3_seed":{"seed_name":"bad","slot":1},"bridge":{"locations":[{"source_table":"VBonus","source_id":"missing"}]}})", native_catalog::CheckKind::VBonus); }));
    assert(fails([] { native_catalog::check_setup(R"({"kh3_seed":{"seed_name":"bad","slot":1},"bridge":{"locations":[{"source_id":"duplicate","type":"treasure"},{"source_id":"duplicate","type":"treasure"}]}})", native_catalog::CheckKind::Chest); }));
    assert(fails([] { native_catalog::check_setup(R"({"schema":"kh3ap.event_config.v1","seed_name":"old","slot":1,"flags":[]})", native_catalog::CheckKind::Event); }));
    std::cout << "Native item decoding and seed check setup passed\n";
}
