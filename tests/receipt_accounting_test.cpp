#include "kh3ap/bridge/rewards/receipt_accounting.h"
#include "kh3ap/bridge/serialization/json.h"
#include <cassert>
#include <iostream>

using namespace kh3ap::receipt_accounting;
int main()
{
    Accounting model;
    Json::object_t descriptions{{"0", Json{{"index", 0}, {"native_supported", true}, {"item_name", "Unlock: Magic"},
        {"location_name", "Always On - Magic"}, {"action", Json{{"kind", "grant_controller_unlock"}, {"unlock_display_name", "Magic"}}}}},
        {"1", Json{{"index", 1}, {"native_supported", true}}},
        {"2", Json{{"index", 2}, {"native_supported", false}}}};
    std::map<int64_t, Json> facts{{0, descriptions.at("0")}, {1, descriptions.at("1")}, {2, descriptions.at("2")}};
    std::map<int64_t, std::string> policies{{0, "save_load_replay"}, {1, "save_load_replay"}, {2, "none"}};
    Json identity{{"owner", "dll"}, {"process_id", 777}, {"seed_name", "test"}, {"slot", 1}, {"delivery_epoch", 4}};
    const auto project = [&](const Accounting& m, const std::set<int64_t>& active, const std::vector<int64_t>& pending) {
        return m.project(identity, descriptions, facts, policies, active, pending, Json{}, Json::array_t{}, Json::array_t{}, true, "ready");
    };
    const auto rejects = [](auto operation) { bool rejected{}; try { operation(); } catch (const std::exception&) { rejected = true; } assert(rejected); };
    Json raw{{"request_id", "first"}, {"status", "partial"}, {"applied_at", "2026-10-05T12:00:00Z"},
        {"applied_indices", Json::array_t{0}}, {"action_kind", "runtime_item_grant_batch"}};
    rejects([&] { model.complete(raw, {}, descriptions, 2); });
    model.remember({0}, "2026-10-05T12:00:00Z");
    const auto ack = model.complete(raw, {0}, descriptions, 2);
    assert(ack["indices"].get_ref<const Json::array_t&>() == Json::array_t{0});
    assert(ack["client_notifications"].get_ref<const Json::array_t&>().size() == 2);
    assert(ack["client_notifications"][0]["message"].get_ref<const std::string&>() == "KH3 item applied: Unlock: Magic (Starting Inventory)");
    assert(ack["client_notifications"][1]["message"].get_ref<const std::string&>() == "Magic unlocked");
    const auto initial = project(model, {0}, {1, 2});
    assert(initial.at("item_consumer_plan.json")["pending_action_count"].get<double>() == 2);
    assert(initial.at("item_consumer_plan.json")["grantable_action_count"].get<double>() == 1);
    Accounting restored; restored.restore(model.checkpoint());
    std::string saved, recovered;
    kh3ap::json::write(model.checkpoint(), saved); kh3ap::json::write(restored.checkpoint(), recovered);
    assert(saved == recovered);
    // An older loaded save replays the item while global acknowledgement
    // history and the first application timestamp remain intact.
    const auto rollback = project(restored, {}, {0, 1, 2});
    assert(rollback.at("item_delivery.json")["applied_item_indices"].get_ref<const Json::array_t&>().empty());
    assert(rollback.at("item_delivery_ledger_summary.json")["applied_count"].get<double>() == 1);
    assert(rollback.at("item_delivery_state.json")["applied_item_indices"].get_ref<const Json::array_t&>() == Json::array_t{0});
    raw["request_id"] = "replay";
    const auto replay = restored.complete(raw, {0}, descriptions, 2);
    assert(replay["client_notifications"].get_ref<const Json::array_t&>().size() == 1); // Replay logs application, not another unlock.
    assert(text(replay["event_key"]) != text(ack["event_key"]));
    raw["applied_indices"] = Json::array_t{0, 0};
    assert(restored.complete(raw, {0}, descriptions, 2)["indices"].get_ref<const Json::array_t&>().size() == 1);
    raw["applied_indices"] = Json::array_t{-1};
    assert(restored.complete(raw, {}, descriptions, 2)["indices"].get_ref<const Json::array_t&>().empty());
    raw["applied_indices"] = Json::array_t{1000001};
    rejects([&] { restored.complete(raw, {0}, descriptions, 2); });
    auto corrupt = model.checkpoint(); corrupt["applied"].get_ref<Json::array_t&>().push_back(corrupt["applied"].get_ref<const Json::array_t&>().front());
    rejects([&] { restored.restore(corrupt); });
    corrupt = model.checkpoint(); corrupt["version"] = 2;
    rejects([&] { restored.restore(corrupt); });
    corrupt = model.checkpoint(); corrupt.get_ref<Json::object_t&>().erase("last_ack");
    rejects([&] { restored.restore(corrupt); });
    rejects([&] { integer(Json(1.5)); });
    rejects([&] { integer(Json(1e30)); });
    Json::object_t preview_descriptions;
    for (int i = 0; i < 6; ++i) preview_descriptions.emplace(std::to_string(i), Json{
        {"index", i}, {"item_name", "Item " + std::to_string(i)}, {"location_id", i}, {"from_player", 2}});
    preview_descriptions["4"]["location_id"] = -2;
    preview_descriptions["5"]["from_player"] = 0; preview_descriptions["5"]["location_name"] = "Server";
    kh3ap::reconciliation::Receipts sparse{0, {2}};
    const auto preview = kh3ap::client_presentation::replay_items(sparse, {0, 1, 2, 3, 4, 5}, preview_descriptions);
    assert(preview.get_ref<const Json::array_t&>().size() == 2);
    assert(preview[0]["index"].get<double>() == 1 && preview[1]["index"].get<double>() == 3);
    const auto recovered_preview = kh3ap::client_presentation::replay_items(sparse, {0, 1, 2, 3, 4, 5, 6}, preview_descriptions);
    assert(recovered_preview.get_ref<const Json::array_t&>() == preview.get_ref<const Json::array_t&>());
    preview_descriptions.emplace("6", Json{{"index", 6}, {"item_name", "Recovered item"}, {"location_id", 6}, {"from_player", 2}});
    const auto complete_preview = kh3ap::client_presentation::replay_items(sparse, {0, 1, 2, 3, 4, 5, 6}, preview_descriptions);
    assert(complete_preview.get_ref<const Json::array_t&>().size() == 3);
    assert(complete_preview[2]["index"].get<double>() == 6);
    Json keyblade{{"index", 7}, {"item_name", "Crystal Snow"}, {"location_name", "Arendelle"},
        {"action", Json{{"kind", "grant_keyblade"}}}, {"kh3_secondary_rewards", Json::array_t{"Bonus: MP +3", "Magic: Cure"}}};
    const auto notices = kh3ap::client_presentation::applied_notifications({keyblade, keyblade}, {7});
    assert(notices.get_ref<const Json::array_t&>().size() == 3); // Batch duplicates share each notice.
    assert(notices[1]["message"].get_ref<const std::string&>() == "Bundle extras: Crystal Snow (Arendelle) [+ Bonus: MP +3, Magic: Cure]");
    assert(notices[2]["message"].get_ref<const std::string&>() == "Keyblade received: Crystal Snow");
    std::cout << "Native receipt accounting checkpoint, partial ACK, rollback and consumer projection tests passed\n";
}
