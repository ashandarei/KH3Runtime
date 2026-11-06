#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/features/kh02/progress/kh02_check_policy.h"
#include "kh3ap/features/kh02/progress/kh02_progress_codec.h"
#include "kh3ap/features/kh02/progress/kh02_gameflow_policy.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/serialization/request_parser.h"
#include "chest_fixture.h"
#include "kh3ap/bridge/serialization/json.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <span>
#include <source_location>
#include <thread>

using Json = kh3ap::json::Value;
using namespace kh3ap;
namespace
{
    std::mutex test_mutex;
    std::shared_ptr<const RuntimeItemGrantBatchRequest> trap_request;
    int trap_submissions{};
    std::vector<Json> notifications;
    std::vector<Json> receipt_events;
    std::map<std::string, Json> published;
    std::map<std::string, size_t> publication_counts;
    std::shared_ptr<const json::SeedDocument> pak_seed_fixture;
    auto pak_seed_source(std::string_view name) -> std::shared_ptr<const json::SeedDocument>
    { return name == "slot_data.json" ? pak_seed_fixture : nullptr; }
    auto embed_bundle(const std::filesystem::path& directory, std::string_view text) -> void
    {
        const auto path = directory / "slot_data.json";
        auto seed = json::read_seed_document(path)->value;
        seed["runtime_reward_bundles"] = Json::array({Json::parse(text)});
        std::ofstream(path) << seed.dump();
    }
    auto encode(const Json& j) -> std::string { std::string result; kh3ap::json::write(j, result); return result; }
    auto complete_receipts(std::span<const int64_t> indices, int64_t epoch) -> void
    {
        static int64_t serial{};
        Json::array_t completed;
        for (const auto index : indices) completed.emplace_back(index);
        reconciliation::observe("runtime_item_apply_event.json", encode(Json{
            {"owner", "dll"}, {"process_id", GetCurrentProcessId()}, {"seed_name", "test"}, {"slot", 1},
            {"delivery_epoch", epoch}, {"request_id", "host-completion-" + std::to_string(++serial)},
            {"status", "applied"}, {"applied_at", "2026-10-05T12:00:00Z"},
            {"action_kind", "runtime_item_grant_batch"}, {"applied_indices", std::move(completed)}}));
    }
    auto until(const auto& predicate, std::source_location location = std::source_location::current()) -> void
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() > deadline)
            { std::cerr << "Worker timeout at line " << location.line() << ": " << reconciliation::view()->error << '\n'; std::abort(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}
namespace kh3ap::pipe
{
    auto write(const std::filesystem::path& path, std::string_view value) -> bool
    {
        if (path.extension() == ".json")
        { Json j; assert(kh3ap::json::parse(j, value)); std::lock_guard lock{test_mutex};
            ++publication_counts[path.filename().string()]; published[path.filename().string()] = std::move(j); }
        if (path.filename() == "runtime_notification_event.json")
        { Json j; assert(kh3ap::json::parse(j, value)); std::lock_guard lock{test_mutex}; notifications.push_back(std::move(j)); }
        if (path.filename() == "runtime_receipt_ack_event.json")
        { Json j; assert(kh3ap::json::parse(j, value)); std::lock_guard lock{test_mutex}; receipt_events.push_back(std::move(j)); }
        return true;
    }
    auto notify_grants() -> void {}
    auto erase(const std::filesystem::path& path) -> void
    { std::lock_guard lock{test_mutex}; published.erase(path.filename().string()); }
}
namespace kh3ap::native_traps
{
    auto is_trap(const RuntimeItemGrantRequest& g) -> bool
    { return g.grant_method == L"direct_player_bad_status" && g.runtime_token == L"ETresBadStatusType::TRES_BADSTAT_BURN"; }
    auto submit(std::shared_ptr<const RuntimeItemGrantBatchRequest> request) -> void
    { std::lock_guard lock{test_mutex}; trap_request = std::move(request); ++trap_submissions; }
}
int main()
{
    // Recovery edits the private bank and preserves unrelated chapter progress.
    const auto chapter = kh02_gameflow::update(nullptr, "gameflow_dw", 110);
    assert(kh02_gameflow::read(nullptr, "gameflow_DW") == 102);
    assert(kh02_gameflow::read(chapter, "GAMEFLOW_DW") == 110);
    auto collected = kh02_gameflow::update(chapter, "gameflow_DW_GEAR_GETCOUNT", 3);
    collected["challenges"]["22"] = 2;
    collected["counts"]["22"] = 20;
    const auto rewind = kh02_gameflow::update(collected, "gameflow_dw_gear_getcount", 0);
    assert(rewind["flags"].size() == 2);
    assert(rewind["challenges"] == collected["challenges"]);
    assert(rewind["counts"] == collected["counts"]);
    assert(kh02_gameflow::read(rewind, "gameflow_DW_GEAR_GETCOUNT") == 0);
    assert(kh02_gameflow::read(rewind, "gameflow_DW_VISITED_2") == 0);
    for (const auto& [key, value] : std::vector<std::pair<std::string, int>>{
             {"gameflow_TT", 1}, {"gameflow_DW", 99}, {"gameflow_DW", 111},
             {"gameflow_DW", 10000}, {"gameflow_DW_GEAR_GETCOUNT", -2}})
    {
        bool rejected = false;
        try
        {
            kh02_gameflow::update(collected, key, value);
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        assert(rejected);
        assert(collected["flags"]["gameflow_DW_GEAR_GETCOUNT"] == 3);
    }
    const auto parent = std::filesystem::canonical(std::filesystem::current_path());
    const auto root = parent / ("native-delivery-worker-" + std::to_string(GetCurrentProcessId()));
    assert(root.parent_path() == parent && !std::filesystem::exists(root));
    std::filesystem::create_directory(root);
    const auto converted = chest_fixture::vanilla(); const auto placed = chest_fixture::placed();
    const auto converted_id = kh3ap::chest_manifest::integer(converted["ap_location_id"]);
    const auto placed_id = kh3ap::chest_manifest::integer(placed["ap_location_id"]);
    std::ofstream(root / "slot_data.json") << encode(chest_fixture::seed({converted, placed}));
    Json::array_t rows;
    for (int index = 0; index < 5; ++index)
    {
        const bool trap = index == 4;
        rows.emplace_back(Json{{"index", index}, {"item_id", trap ? 1263022596 : 1263022577},
            {"from_player", 2}, {"location_id", -1}});
    }
    Json input{{"schema", "kh3ap.receipt_catalog.v4"}, {"process_id", GetCurrentProcessId()},
        {"seed_name", "test"}, {"slot", 1}, {"items", rows}, {"server_checked_locations", Json::array_t{}}};
    std::ofstream(root / reconciliation::journal_file) << encode(Json{{"schema", "kh3ap.native_reconciliation.v4"},
        {"receipt_accounting", Json{{"version", 1}, {"applied", Json::array_t{}}, {"last_ack", Json{}}}},
        {"seed_name", "test"}, {"slot", 1}, {"complete", true}, {"goal_complete", false},
        {"hint_report_numbers", Json::array_t{}}, {"observed_checks", Json::array_t{}}, {"completed_checks", Json::array_t{}},
        {"forced_indices", Json::array_t{}}, {"repair_keys", Json::array_t{}}, {"catalog", Json::array_t{}},
        {"slots", Json::array_t{Json{{"save_slot", 0}, {"saved_receipts", Json{{"version", 1}, {"through", 0}, {"extra", Json::array_t{2}}}}}}}});
    reconciliation::start(root);
    until([] { return reconciliation::view()->chests != nullptr; });
    const auto initial_chests = reconciliation::view()->chests;
    const auto converted_key = std::wstring(converted["source_id"].get_ref<const std::string&>().begin(), converted["source_id"].get_ref<const std::string&>().end());
    const auto placed_key = std::wstring(placed["source_id"].get_ref<const std::string&>().begin(), placed["source_id"].get_ref<const std::string&>().end());
    assert(initial_chests->bridge_dir == root && initial_chests->process_id == GetCurrentProcessId());
    assert(initial_chests->seed == L"test" && initial_chests->slot == 1 && !reconciliation::view()->ready);
    assert(!initial_chests->assignments.at(placed_key).cleared);
    // External clears are usable before any AP receipt catalog/gameplay load.
    reconciliation::observe("runtime_mom_clear_state.json", encode(chest_fixture::clears({placed_id})));
    until([&] { const auto typed = reconciliation::view()->chests; return typed && typed->assignments.at(placed_key).cleared; });
    const auto cleared_chests = reconciliation::view()->chests;
    assert(!initial_chests->assignments.at(placed_key).cleared && cleared_chests->same_layout(*initial_chests));
    until([] { std::lock_guard lock{test_mutex}; return published.contains("runtime_music_chests.json") &&
        published.at("runtime_music_chests.json")["chests"].get_ref<const Json::array_t&>()[1]["cleared"].get<bool>(); });
    {
        std::lock_guard lock{test_mutex};
        const auto& chest_rows = published.at("runtime_music_chests.json")["chests"].get_ref<const Json::array_t&>();
        assert(!chest_rows[0]["cleared"].get<bool>());
    }
    input["server_checked_locations"] = Json::array_t{converted_id, placed_id};
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    until([] { return reconciliation::view()->expanded != nullptr; });
    until([&] { return reconciliation::view()->chests->assignments.at(converted_key).cleared; });
    assert(!cleared_chests->assignments.at(converted_key).cleared);
    until([] { std::lock_guard lock{test_mutex}; return
        published.at("runtime_music_chests.json")["chests"].get_ref<const Json::array_t&>()[0]["cleared"].get<bool>(); });
    assert(!reconciliation::view()->delivery); // Catalog publication is not gameplay readiness.
    // Synthetic lifecycle events only; no vanilla save is opened or inspected.
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 1, 0);
    until([] { return reconciliation::view()->delivery != nullptr; });
    const auto initial = reconciliation::view();
    assert(initial->ready && initial->active.contains(0) && initial->active.contains(2));
    assert(initial->delivery->grants.size() == 2);
    assert(initial->delivery->grants[0].index == 1 && initial->delivery->grants[0].runtime_token.ends_with(L"::2"));
    assert(initial->delivery->grants[1].index == 3 && initial->delivery->grants[1].runtime_token.ends_with(L"::4"));
    until([] { std::lock_guard lock{test_mutex}; return published["runtime_reconciliation_state.json"]["ready"].get<bool>(); });
    until([] { std::lock_guard lock{test_mutex}; return published.contains("runtime_item_descriptions.json"); });
    double description_revision{};
    {
        std::lock_guard lock{test_mutex};
        const auto& summary = published.at("runtime_reconciliation_state.json")["delivery_summary"];
        assert(summary["received_count"].get<double>() == 5 && summary["applied_count"].get<double>() == 2);
        assert(summary["pending_count"].get<double>() == 3 && summary["last_applied_index"].get<double>() == 0);
        const auto& descriptions = published.at("runtime_item_descriptions.json");
        description_revision = descriptions["revision"].get<double>();
        assert(descriptions["items"]["3"]["kh3_runtime_token"].get_ref<const std::string&>().ends_with("::4"));
        assert(description_revision == published.at("runtime_reconciliation_state.json")["description_revision"].get<double>());
        const auto& slot = published.at("runtime_save_receipts.json")["slots"]["kh3sv2/data/KHIII_slot0.bin"];
        assert(slot["replay_preview_ready"].get<bool>());
        const auto& preview = slot["pending_replay_items"].get_ref<const Json::array_t&>();
        assert(preview.size() == 2 && preview[0]["index"].get<double>() == 1 && preview[1]["index"].get<double>() == 3);
    }
    until([] { std::lock_guard lock{test_mutex}; return trap_request != nullptr; });
    std::shared_ptr<const RuntimeItemGrantBatchRequest> admitted;
    { std::lock_guard lock{test_mutex}; admitted = trap_request; }
    Json ack{{"process_id", GetCurrentProcessId()}, {"seed_name", "test"}, {"slot", 1}, {"status", "accepted"},
        {"request_id", std::string(admitted->request_id.begin(), admitted->request_id.end())}, {"indices", Json::array_t{4}}};
    reconciliation::observe("runtime_trap_queue_ack.json", encode(ack));
    until([] { return reconciliation::view()->active.contains(4); });
    assert(!reconciliation::view()->active.contains(1));
    Json completed_ack{{"owner", "dll"}, {"process_id", GetCurrentProcessId()}, {"seed_name", "test"},
        {"slot", 1}, {"delivery_epoch", initial->epoch}, {"request_id", "worker-ack-1"}, {"status", "partial"},
        {"applied_indices", Json::array_t{1, 1}}, {"applied_at", "2026-10-05T12:00:00Z"}, {"action_kind", "runtime_item_grant_batch"}};
    reconciliation::observe("runtime_item_apply_event.json", encode(completed_ack));
    until([] { std::lock_guard lock{test_mutex}; return published.contains("runtime_receipt_ack_event.json"); });
    assert(reconciliation::view()->active.contains(1));
    {
        std::lock_guard lock{test_mutex};
        const auto& receipt_ack = published.at("runtime_receipt_ack_event.json");
        assert(receipt_ack["schema"].get_ref<const std::string&>() == "kh3ap.receipt_ack.v1");
        assert(receipt_ack["indices"].get_ref<const Json::array_t&>().size() == 1);
        assert(published.at("runtime_item_apply_state.json")["last_processed_event_key"].get_ref<const std::string&>() == receipt_ack["event_key"].get_ref<const std::string&>());
        std::ifstream durable(root / reconciliation::journal_file);
        Json journal; assert(kh3ap::json::parse(journal, std::string(std::istreambuf_iterator<char>(durable), {})));
        assert(journal["receipt_accounting"]["last_ack"]["request_id"].get_ref<const std::string&>() == "worker-ack-1");
    }
    until([] { std::lock_guard lock{test_mutex}; return !notifications.empty(); });
    {
        std::lock_guard lock{test_mutex};
        assert(notifications.back()["event"].get_ref<const std::string&>() == "item_applied");
        assert(notifications.back()["schema"].get_ref<const std::string&>() == "kh3ap.notification.v1");
        assert(notifications.back()["request_id"].get_ref<const std::string&>().starts_with("native-receipt-notification-"));
    }
    const auto ticks = [] { FILETIME ft{}; GetSystemTimeAsFileTime(&ft); return uint64_t(ft.dwHighDateTime) << 32 | ft.dwLowDateTime; }();
    reconciliation::auto_save_begin(ticks);
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_WRITE, 2, ticks);
    reconciliation::file_closed(2, true);
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 3, ticks);
    until([&] { return reconciliation::view()->epoch > initial->epoch && reconciliation::view()->ready; });
    const auto restored = reconciliation::view();
    assert(restored->active.contains(1) && restored->active.contains(4) && !restored->active.contains(3));
    assert(restored->delivery->grants.size() == 1 && restored->delivery->grants[0].runtime_token.ends_with(L"::4"));
    const int64_t stale[] = {3}; complete_receipts(stale, initial->epoch);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    until([&] { return reconciliation::view()->delivery_revision > restored->delivery_revision; });
    assert(!reconciliation::view()->active.contains(3));
    until([&] { std::lock_guard lock{test_mutex}; return
        published.at("runtime_reconciliation_state.json")["delivery_epoch"].get<double>() == restored->epoch; });
    {
        std::lock_guard lock{test_mutex};
        const auto& summary = published.at("runtime_reconciliation_state.json")["delivery_summary"];
        assert(summary["applied_count"].get<double>() == 4 && summary["pending_count"].get<double>() == 1);
        assert(summary["last_applied_index"].get<double>() == 2 && summary["highest_received_index"].get<double>() == 4);
        assert(published.at("runtime_item_descriptions.json")["revision"].get<double>() == description_revision);
    }
    // Pawn discovery can precede the authoritative room-entry event for the
    // same pawn. It must not prevent the retry checkpoint from being captured.
    Json room{{"hook", "RuntimePlayablePawnContext"}, {"pawn_source", "CurrentPossession"},
        {"object", "Sora /Game/Levels/tt/tt_01/tt_01:First"}, {"opened_at", "2026-10-04T12:00:00Z"}};
    reconciliation::observe("runtime_context.json", encode(room));
    room["pawn_source"] = "VanillaLevelWritePolyhook:level_owner";
    reconciliation::observe("runtime_context.json", encode(room));
    // Ordering on the worker's queue captures the room before adding receipt 3.
    complete_receipts(stale, restored->epoch);
    until([] { return reconciliation::view()->active.contains(3); });
    reconciliation::observe("current_state_write_polyhook_events.jsonl",
        R"({"state_class":"TresPlayerState_GameOver","state_comp_matches_cached_pawn":true})");
    until([] { return !reconciliation::view()->ready; });
    room["object"] = "Sora /Game/Levels/tt/tt_01/tt_01:Second";
    room["pawn_source"] = "RuntimePlayablePawnContext";
    reconciliation::observe("runtime_context.json", encode(room));
    until([] { return reconciliation::view()->ready; });
    assert(!reconciliation::view()->active.contains(3));
    assert(reconciliation::view()->active.contains(1));
    assert(reconciliation::view()->active.contains(4));
    assert(reconciliation::view()->delivery->grants[0].runtime_token.ends_with(L"::4"));

    // Opening Darkside has no disk checkpoint yet. Its native map-ready event
    // must preserve starting receipts even if pawn discovery happened first.
    // Prepare & Retry can reuse a pawn, or publish recovery only upon GoA entry.
    reconciliation::stop();
    const auto saved_trap_request = trap_request;
    const auto saved_trap_submissions = trap_submissions;
    for (const bool delayed_until_garden : {false, true})
    {
        reconciliation::stop();
        const auto opening_root = root / (delayed_until_garden ? "opening-delayed" : "opening-same-pawn");
        std::filesystem::create_directory(opening_root);
        std::ofstream(opening_root / "slot_data.json") << encode(chest_fixture::seed({converted, placed}));
        reconciliation::start(opening_root);
        reconciliation::observe("runtime_receipt_catalog.json", encode(input));
        reconciliation::observe("runtime_new_game_staging.json", R"({"hook":"RuntimePlayablePawnContext"})");
        until([] { return reconciliation::view()->ready; });
        const int64_t starting[] = {0, 1, 2, 4};
        complete_receipts(starting, reconciliation::view()->epoch);
        until([] { return reconciliation::view()->active.contains(2) && reconciliation::view()->active.contains(4); });
        Json opening{{"hook", "RuntimePlayablePawnContext"}, {"pawn_source", "CurrentPossession"},
            {"object", "Sora /Game/Levels/ew_DLC/ew_04/ew_04:First"}, {"opened_at", "opening-entry"}};
        reconciliation::observe("runtime_context.json", encode(opening));
        opening["schema"] = "kh3ap.runtime_map_ready.v1";
        opening["source"] = "MapName:UIPMapNameCallback";
        reconciliation::observe("runtime_map_ready.json", encode(opening));
        complete_receipts(stale, reconciliation::view()->epoch);
        until([] { return reconciliation::view()->active.contains(3); });
        // Repeated ready callbacks cannot advance the frozen room checkpoint.
        reconciliation::observe("runtime_map_ready.json", encode(opening));
        reconciliation::observe("current_state_write_polyhook_events.jsonl",
            R"({"state_class":"TresPlayerState_GameOver","state_comp_matches_cached_pawn":true})");
        until([] { return !reconciliation::view()->ready; });
        if (delayed_until_garden)
        {
            opening["object"] = "Sora /Game/Maps/GardenOfAssemblage/umap/GoA_01:Second";
            reconciliation::observe("runtime_context.json", encode(opening));
        }
        else
        {
            // Cached context while dead must not itself consume the death fence.
            reconciliation::observe("runtime_context.json", encode(opening));
            reconciliation::observe("runtime_map_ready.json", encode(opening));
        }
        until([] { return reconciliation::view()->ready; });
        const auto recovered = reconciliation::view();
        assert(recovered->active.contains(0) && recovered->active.contains(1) && recovered->active.contains(2));
        assert(recovered->active.contains(4) && !recovered->active.contains(3));
        assert(recovered->delivery && recovered->delivery->grants.size() == 1 && recovered->delivery->grants[0].index == 3);
    }
    reconciliation::stop();
    trap_request = saved_trap_request;
    trap_submissions = saved_trap_submissions;
    reconciliation::start(root);
    // Client-owned external-result checkpoint is recovered without a client connection.
    std::ofstream(root / "runtime_mom_clear_state.json") << encode(chest_fixture::clears({placed_id}));
    reconciliation::stop();
    reconciliation::start(root);
    reconciliation::observe("runtime_mom_clear_state.json", encode(chest_fixture::clears({999})));
    until([] { return !reconciliation::view()->error.empty(); });
    assert(!reconciliation::view()->chests); // Failed validation clears the internal snapshot too.
    assert(initial_chests->assignments.size() == 2); // Readers may safely retain prior immutable values.
    reconciliation::stop();
    {
        std::lock_guard lock{test_mutex};
        published.erase("runtime_music_chests.json"); published.erase("runtime_keyblade_chests.json");
    }

    // Restart reconstructs the durable one-shot fence and external clears, then waits for AP templates.
    reconciliation::start(root);
    until([] { std::lock_guard lock{test_mutex}; return published.contains("runtime_music_chests.json") &&
        published.at("runtime_music_chests.json")["chests"].get_ref<const Json::array_t&>()[1]["cleared"].get<bool>(); });
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot98.bin", GENERIC_READ, 4, ticks);
    until([] { return reconciliation::view()->ready; });
    assert(reconciliation::view()->active.contains(4));
    { std::lock_guard lock{test_mutex}; assert(trap_submissions == 1); }
    reconciliation::stop();
    // Event/VBonus repair ownership requires no client command or round trip.
    const auto loot_root = root / "loot";
    std::filesystem::create_directory(loot_root);
    std::ofstream(loot_root / "slot_data.json") << R"({"kh3_seed":{"seed_name":"test","slot":1},"bridge":{"locations":[
        {"ap_location_id":200,"bridge_key":"VBonus:Vbonus_016","source_id":"Vbonus_016","type":"vbonus","source_table":"VBonus","ap_location_name":"Boss bonus","assigned_item_name":"Aerial Recovery"},
        {"ap_location_id":201,"bridge_key":"Event:POTION","source_id":"POTION","type":"event","source_table":"Event","ap_location_name":"Potion event","assigned_item_name":"Potion"},
        {"ap_location_id":202,"bridge_key":"Event:DELAYED","source_id":"DELAYED","type":"event","source_table":"Event","ap_location_name":"Delayed bonus","assigned_item_name":"Aerial Recovery"},
        {"bridge_key":"Event:FOREIGN","source_id":"FOREIGN","type":"event","source_table":"Event","ap_location_name":"Foreign event","assigned_item_name":"Missile Tank","assigned_item_player_name":"Samus","assigned_item_classification":1,"is_foreign_item":true},
        {"bridge_key":"LevelUp:4","source_id":"4","type":"level_up","source_table":"LevelUp","ap_location_name":"Level 4","assigned_item_name":"Energy Tank","is_foreign_item":true}
    ]}})";
    auto make_row = [](int index, const char* key, const char*, bool inventory) {
        const std::map<std::string, int> ids{{"VBonus:Vbonus_016", 200}, {"Event:POTION", 201},
            {"Event:DELAYED", 202}, {"Event:EVENT_KEYBLADE_001", 100}, {"Event:FORCED", 101}};
        return Json{{"index", index}, {"item_id", inventory ? 1263022503 : 1263022199},
            {"from_player", 1}, {"location_id", ids.at(key)}};
    };
    auto bonus = make_row(0, "VBonus:Vbonus_016", "durable_sora_ability", false);
    auto potion = make_row(1, "Event:POTION", "bp_tres_get_item", true);
    embed_bundle(loot_root, R"({"bridge_key":"VBonus:Vbonus_016","randomized_tokens":["ETresAbilityKind::AIR_RECOVERY","ITEM_POTION","ETresAbilityKind::AIRSLIDE"]})");
    input["items"] = Json::array_t{bonus, potion};
    std::ofstream(loot_root / "vbonus_progress.json") << R"({"schema":"kh3ap.vbonus_progress.v1","seed_name":"test","slot":1,"completed":["Vbonus_016"]})";
    std::ofstream(loot_root / "event_progress.json") << R"({"schema":"kh3ap.event_progress.v1","seed_name":"test","slot":1,"completed":["POTION","DELAYED"]})";
    reconciliation::start(loot_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 10, ticks);
    until([] { return reconciliation::view()->ready; });
    assert(!reconciliation::view()->delivery);
    auto event = Json{{"type", "vbonus"}, {"bridge_key", "VBonus:Vbonus_016"}, {"opened_at", "first"}, {"actor_name", "boss"}};
    reconciliation::observe("runtime_loot_event.json", encode(event));
    until([] { return reconciliation::view()->delivery != nullptr; });
    const auto repair = reconciliation::view();
    assert(!repair->active.contains(0) && repair->active.contains(1));
    assert(repair->delivery->grants.size() == 2);
    for (const auto& grant : repair->delivery->grants)
        assert(grant.index == 0 && grant.grant_method == L"durable_sora_ability" && !grant.show_info);
    const auto repair_context = repair->delivery->grants.front().reapply_context_key;
    const int64_t repaired[] = {0}; complete_receipts(repaired, repair->epoch);
    until([] { return reconciliation::view()->active.contains(0) && !reconciliation::view()->delivery; });
    // An AP reconnect's stale natural ACK cannot retire an open native repair.
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 11, ticks);
    until([&] { return reconciliation::view()->ready && reconciliation::view()->epoch > repair->epoch; });
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    until([&] { return reconciliation::view()->delivery_revision > repair->delivery_revision; });
    assert(!reconciliation::view()->active.contains(0));
    assert(reconciliation::view()->delivery->grants.size() == 2);
    complete_receipts(repaired, reconciliation::view()->epoch);
    until([] { return !reconciliation::view()->delivery; });
    event["opened_at"] = "duplicate-after-retry";
    reconciliation::observe("runtime_loot_event.json", encode(event));
    until([] { return reconciliation::view()->delivery != nullptr; });
    assert(reconciliation::view()->delivery->grants.front().reapply_context_key != repair_context);
    const auto second = reconciliation::view();
    reconciliation::observe("runtime_loot_event.json", encode(event));
    complete_receipts(repaired, second->epoch);
    until([] { return !reconciliation::view()->delivery; });
    event["type"] = "event"; event["bridge_key"] = "Event:POTION"; event["opened_at"] = "inventory";
    reconciliation::observe("runtime_loot_event.json", encode(event));
    until([] { return reconciliation::view()->message != nullptr; });
    assert(reconciliation::view()->message->message == L"AP: Already obtained Potion");
    assert(!reconciliation::view()->delivery);
    reconciliation::message_completed(reconciliation::view()->message->request_id, second->epoch);
    until([] { return !reconciliation::view()->message; });
    event["bridge_key"] = "Event:DELAYED"; event["opened_at"] = "delayed";
    reconciliation::observe("runtime_loot_event.json", encode(event));
    // Synchronize the worker before supplying the delayed receipt template.
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    until([&] { return reconciliation::view()->delivery_revision > second->delivery_revision; });
    assert(!reconciliation::view()->delivery && !reconciliation::view()->message);
    input["items"].get_ref<Json::array_t&>().push_back(make_row(2, "Event:DELAYED", "durable_sora_ability", false));
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    until([] { return reconciliation::view()->delivery != nullptr; });
    assert(reconciliation::view()->delivery->grants.size() == 1 && reconciliation::view()->delivery->grants.front().index == 2);
    event["bridge_key"] = "Event:FOREIGN"; event["opened_at"] = "foreign";
    reconciliation::observe("runtime_loot_event.json", encode(event));
    reconciliation::observe("runtime_level_up_checks.json",
        R"({"schema":"kh3ap.level_up_progress.v1","seed_name":"test","slot":1,"completed":["4"]})");
    until([] { std::lock_guard lock{test_mutex}; return std::count_if(notifications.begin(), notifications.end(),
        [](const Json& notice) { return notice["event"].get_ref<const std::string&>() == "runtime_loot"; }) >= 6; });
    const auto first_message = reconciliation::view()->message;
    assert(first_message && first_message->message == L"AP: Found Samus's Missile Tank (p)");
    reconciliation::message_completed(first_message->request_id, second->epoch - 1);
    reconciliation::message_completed(first_message->request_id, second->epoch);
    until([&] { const auto message = reconciliation::view()->message; return message && message->request_id != first_message->request_id; });
    assert(reconciliation::view()->message->message == L"AP: Found Energy Tank");
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 12, ticks);
    until([&] { return reconciliation::view()->ready && reconciliation::view()->epoch > second->epoch; });
    assert(!reconciliation::view()->message); // Old-epoch messages cannot cross a load.
    reconciliation::stop();

    reconciliation::start(loot_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 13, ticks);
    until([] { return reconciliation::view()->ready; });
    assert(reconciliation::view()->delivery && reconciliation::view()->delivery->grants.size() == 3);
    assert(!reconciliation::view()->active.contains(0)); // Native repair ownership survives restart.
    const int64_t all_repaired[] = {0, 2}; complete_receipts(all_repaired, reconciliation::view()->epoch);
    until([] { return !reconciliation::view()->delivery; });
    // A bridge-owned receipt repairs durable members on reobservation, then
    // replays its full AP bundle (including inventory) after a later load.
    reconciliation::stop();
    std::ifstream prior_seed(loot_root / "slot_data.json");
    Json runtime_owned_seed;
    assert(kh3ap::json::parse(runtime_owned_seed, std::string(std::istreambuf_iterator<char>(prior_seed), {})));
    prior_seed.close();
    runtime_owned_seed["bridge"]["locations"][0]["bridge_delivery_required"] = true;
    std::ofstream(loot_root / "slot_data.json") << encode(runtime_owned_seed);
    reconciliation::start(loot_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 14, ticks);
    until([] { return reconciliation::view()->ready; });
    complete_receipts(all_repaired, reconciliation::view()->epoch);
    until([] { return !reconciliation::view()->delivery; });
    event["type"] = "vbonus"; event["bridge_key"] = "VBonus:Vbonus_016"; event["opened_at"] = "runtime-owned";
    reconciliation::observe("runtime_loot_event.json", encode(event));
    until([] { return reconciliation::view()->delivery != nullptr; });
    assert(reconciliation::view()->delivery->grants.size() == 2);
    const auto runtime_epoch = reconciliation::view()->epoch;
    complete_receipts(repaired, runtime_epoch);
    until([] { return !reconciliation::view()->delivery; });
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 14, ticks);
    until([&] { return reconciliation::view()->ready && reconciliation::view()->epoch > runtime_epoch; });
    assert(reconciliation::view()->delivery->grants.size() == 4);
    assert(std::count_if(reconciliation::view()->delivery->grants.begin(), reconciliation::view()->delivery->grants.end(),
        [](const auto& grant) { return grant.runtime_token == L"ITEM_POTION"; }) == 1);
    reconciliation::stop();
    // Final check authority, natural receipt ACKs and server-forced delivery.
    const auto check_root = root / "checks";
    std::filesystem::create_directory(check_root);
    std::ofstream(check_root / "slot_data.json") << R"({"kh3_seed":{"seed_name":"test","slot":1},
        "classic_kingdom_checks":2,"start_with_wall_run":1,"ingredients":2,"adversary_checks":1,"olympus_coliseum_pool":1,"bridge":{"conditional_checks":[
        {"trigger_bridge_key":"VBonus:Vbonus_016","ap_location_id":102,"unless_item_id":999},
        {"trigger_bridge_key":"VBonus:Vbonus_016","ap_location_id":103,"unless_item_id":1263022199},
        {"trigger_bridge_key":"VBonus:Vbonus_016","ap_location_id":104,"unless_start_option":"start_with_wall_run"}],"locations":[
        {"bridge_key":"Conditional:Starting","source_table":"Conditional","source_id":"Starting","type":"sequence_break_bonus","ap_location_id":104},
        {"bridge_key":"TreasureTT:TT1","source_table":"TreasureTT","source_id":"TT1","type":"treasure"},
        {"bridge_key":"MusicChest:AP_LBOX_008","source_table":"MusicChest","source_id":"AP_LBOX_008","type":"melody_of_memory","music_chest_placement_version":1,"ap_location_id":1263154184},
        {"bridge_key":"MusicChestBonus:MusicChest:AP_LBOX_008","source_table":"MusicChestBonus","source_id":"MusicChest:AP_LBOX_008","type":"music_chest_bonus","trigger_bridge_key":"MusicChest:AP_LBOX_008"},
        {"bridge_key":"MusicChest:AP_LBOX_007","source_table":"MusicChest","source_id":"AP_LBOX_007","type":"melody_of_memory","music_chest_placement_version":1},
        {"bridge_key":"MusicChestBonus:MusicChest:AP_LBOX_007","source_table":"MusicChestBonus","source_id":"MusicChest:AP_LBOX_007","type":"music_chest_bonus","trigger_bridge_key":"MusicChest:AP_LBOX_007"},
        {"bridge_key":"MelodyOfMemory:70","source_table":"MelodyOfMemory","source_id":"70","type":"melody_of_memory","ap_location_id":9999},
        {"bridge_key":"LevelUp:LEVEL1","source_table":"LevelUp","source_id":"LEVEL1","type":"level_up"},
        {"bridge_key":"LuckyMark:3","source_table":"LuckyMark","source_id":"3","type":"lucky_emblem"},
        {"bridge_key":"OlympusColiseumCup:CUP1","source_table":"OlympusColiseumCup","source_id":"CUP1","type":"olympus_coliseum_cup"},
        {"bridge_key":"Conditional:Bonus","source_table":"Conditional","source_id":"Bonus","type":"sequence_break_bonus","ap_location_id":102},
        {"bridge_key":"Conditional:Excluded","source_table":"Conditional","source_id":"Excluded","type":"sequence_break_bonus","ap_location_id":103},
        {"bridge_key":"MusicChestBonus:TT1","source_table":"MusicChestBonus","source_id":"TT1","type":"music_chest_bonus","trigger_bridge_key":"VBonus:Vbonus_016"},
        {"bridge_key":"Event:EVENT_KEYBLADE_001","source_table":"Event","source_id":"EVENT_KEYBLADE_001","type":"event","ap_location_id":100},
        {"bridge_key":"VBonus:Vbonus_013","source_table":"VBonus","source_id":"Vbonus_013","type":"vbonus"},
        {"bridge_key":"Event:FORCED","source_table":"Event","source_id":"FORCED","type":"event","ap_location_id":101},
        {"bridge_key":"VBonus:Vbonus_016","source_table":"VBonus","source_id":"Vbonus_016","type":"vbonus"},
        {"bridge_key":"VBonusExtra:016","source_table":"VBonusExtra","source_id":"016","type":"vbonus_extra","trigger_bridge_key":"VBonus:Vbonus_016"},
        {"bridge_key":"MinigameReward:RA_MINIGAME_Rank_A_1st","source_table":"MinigameReward","source_id":"RA_MINIGAME_Rank_A_1st","type":"minigame_reward"},
        {"bridge_key":"MinigameReward:RA_MINIGAME_Rank_A","source_table":"MinigameReward","source_id":"RA_MINIGAME_Rank_A","type":"minigame_reward"},
        {"bridge_key":"MinigameReward:RA_MINIGAME_Rank_C","source_table":"MinigameReward","source_id":"RA_MINIGAME_Rank_C","type":"minigame_reward"},
        {"bridge_key":"Event:TresUIMobilePortalDataAsset","source_table":"Event","source_id":"TresUIMobilePortalDataAsset","type":"event"},
        {"bridge_key":"GummiSpace:Battle1","source_table":"GummiSpace","source_id":"Battle1","type":"space"},
        {"bridge_key":"IngredientSpot:SPOT1","source_table":"IngredientSpot","source_id":"SPOT1","type":"ingredient_spot"},
        {"bridge_key":"Adversary:HEARTLESS_08","source_table":"Adversary","source_id":"HEARTLESS_08","type":"adversary"},
        {"bridge_key":"Synthesis:Recipe1","source_table":"Synthesis","source_id":"Recipe1","type":"synthesis"}
    ]}})";
    {
        std::ifstream file(check_root / "slot_data.json");
        Json check_seed;
        assert(kh3ap::json::parse(check_seed, std::string(std::istreambuf_iterator<char>(file), {})));
        file.close();
        for (auto& row : check_seed["bridge"]["locations"].get_ref<Json::array_t&>())
        {
            if (row["source_table"].get_ref<const std::string&>() != "MusicChest") continue;
            const auto key = row["bridge_key"];
            row = chest_fixture::placed(row["source_id"].get_ref<const std::string&>() == "AP_LBOX_008" ? "MOM_CHEST_008" : "MOM_CHEST_007");
            row["bridge_key"] = key;
        }
        std::ofstream(check_root / "slot_data.json") << encode(check_seed);
    }
    input["items"] = Json::array_t{make_row(0, "Event:EVENT_KEYBLADE_001", "durable_sora_ability", false),
        make_row(1, "Event:FORCED", "bp_tres_get_item", true)};
    input["server_checked_locations"] = Json::array_t{};
    // Recover an already-opened special chest without a prior reconciliation journal.
    std::ofstream(check_root / "chest_progress.json") << R"({"schema":"kh3ap.chest_progress.v1","seed_name":"test","slot":1,"completed":["AP_LBOX_007"]})";
    reconciliation::start(check_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 30, ticks);
    until([] { return reconciliation::view()->ready; });
    assert(reconciliation::view()->completed_checks.contains("MusicChest:AP_LBOX_007"));
    assert(reconciliation::view()->completed_checks.contains("MusicChestBonus:MusicChest:AP_LBOX_007"));
    auto synchronize = [&] {
        const auto revision = reconciliation::view()->delivery_revision;
        reconciliation::observe("runtime_receipt_catalog.json", encode(input));
        until([&] { return reconciliation::view()->delivery_revision > revision; });
    };
    reconciliation::observe("runtime_event_checks.json", R"({"schema":"kh3ap.event_progress.v1","seed_name":"test","slot":1,"completed":["EVENT_KEYBLADE_001"]})");
    synchronize();
    assert(!reconciliation::view()->completed_checks.contains("Event:EVENT_KEYBLADE_001"));
    assert(!reconciliation::view()->active.contains(0));
    reconciliation::observe("runtime_vbonus_checks.json", R"({"schema":"kh3ap.vbonus_progress.v1","seed_name":"wrong","slot":1,"completed":["Vbonus_013"]})");
    synchronize(); assert(!reconciliation::view()->active.contains(0));
    reconciliation::observe("runtime_vbonus_checks.json", R"({"schema":"kh3ap.vbonus_progress.v1","seed_name":"test","slot":1,"completed":["Vbonus_013"]})");
    until([] { return reconciliation::view()->active.contains(0); });
    assert(reconciliation::view()->completed_checks.contains("Event:EVENT_KEYBLADE_001"));
    assert(!reconciliation::view()->delivery);
    input["server_checked_locations"] = Json::array_t{100, 101};
    synchronize();
    assert(reconciliation::view()->delivery && reconciliation::view()->delivery->grants.size() == 1);
    assert(reconciliation::view()->delivery->grants.front().index == 1);
    const int64_t forced_done[] = {1}; complete_receipts(forced_done, reconciliation::view()->epoch);
    until([] { return reconciliation::view()->active.contains(1); });
    reconciliation::observe("runtime_context.json", R"({"hook":"BP_TresGetVictoryBonus","outer":"Actor /Game/Levels/tt/tt_01/umap/tt_01_enemy_06"})");
    until([] { return reconciliation::view()->completed_checks.contains("VBonusExtra:016"); });
    assert(reconciliation::view()->completed_checks.contains("Conditional:Bonus"));
    assert(!reconciliation::view()->completed_checks.contains("Conditional:Excluded"));
    assert(!reconciliation::view()->completed_checks.contains("Conditional:Starting"));
    assert(reconciliation::view()->completed_checks.contains("MusicChestBonus:TT1"));
    // Clearing a song only unlocks the placed chest; opening owns its AP check.
    reconciliation::observe("runtime_mom_clear_state.json", R"({"schema":"kh3ap.mom_clear_state.v1","seed_name":"test","slot":1,"cleared_locations":[1263154184]})");
    synchronize();
    assert(!reconciliation::view()->completed_checks.contains("MusicChest:AP_LBOX_008"));
    reconciliation::observe("runtime_chest_checks.json", R"({"schema":"kh3ap.chest_progress.v1","seed_name":"wrong","slot":1,"completed":["AP_LBOX_008"]})");
    synchronize();
    assert(!reconciliation::view()->completed_checks.contains("MusicChest:AP_LBOX_008"));
    reconciliation::observe("runtime_chest_checks.json", R"({"schema":"kh3ap.chest_progress.v1","seed_name":"test","slot":1,"completed":["AP_LBOX_008","70"]})");
    synchronize();
    assert(reconciliation::view()->completed_checks.contains("MusicChest:AP_LBOX_008"));
    assert(reconciliation::view()->completed_checks.contains("MusicChestBonus:MusicChest:AP_LBOX_008"));
    assert(!reconciliation::view()->completed_checks.contains("MelodyOfMemory:70"));
    const auto music_checks = reconciliation::view()->completed_checks;
    reconciliation::observe("runtime_chest_checks.json", R"({"schema":"kh3ap.chest_progress.v1","seed_name":"test","slot":1,"completed":["AP_LBOX_008"]})");
    synchronize();
    assert(reconciliation::view()->completed_checks == music_checks);
    reconciliation::observe("runtime_minigame_rewards.json", R"({"schema":"kh3ap.minigame_rewards.v1","seed_name":"test","slot":1,"source":"remuneration_reward","completed_rewards":["RA_MINIGAME_Rank_A_1st"]})");
    until([] { return reconciliation::view()->completed_checks.contains("MinigameReward:RA_MINIGAME_Rank_C"); });
    assert(!reconciliation::view()->completed_checks.contains("MinigameReward:RA_MINIGAME_Rank_A"));
    reconciliation::observe("runtime_chest_checks.json", R"({"schema":"kh3ap.chest_progress.v1","seed_name":"test","slot":1,"completed":["TT1","UNSELECTED"]})");
    reconciliation::observe("runtime_level_up_checks.json", R"({"schema":"kh3ap.level_up_progress.v1","seed_name":"test","slot":1,"completed":["LEVEL1"]})");
    reconciliation::observe("runtime_lucky_mark_checks.json", R"({"schema":"kh3ap.lucky_mark_progress.v1","seed_name":"test","slot":1,"completed":["3"]})");
    reconciliation::observe("runtime_olympus_coliseum_state.json", R"({"schema":"kh3ap.runtime_olympus_coliseum_state.v1","seed_name":"test","slot":1,"olympus_coliseum_pool":1,"completed_cups":["CUP1"]})");
    synchronize();
    assert(reconciliation::view()->completed_checks.contains("TreasureTT:TT1"));
    assert(reconciliation::view()->completed_checks.contains("LevelUp:LEVEL1"));
    assert(reconciliation::view()->completed_checks.contains("LuckyMark:3"));
    assert(reconciliation::view()->completed_checks.contains("OlympusColiseumCup:CUP1"));
    Json::array_t games;
    for (int i = 1; i <= 23; ++i) games.emplace_back("LSIGAME" + std::string(i < 10 ? "0" : "") + std::to_string(i));
    Json classic{{"schema", "kh3ap.runtime_classic_kingdom_state.v3"}, {"seed_name", "test"}, {"slot", 1},
        {"classic_kingdom_checks", 2}, {"high_score_games", games}};
    reconciliation::observe("runtime_classic_kingdom_state.json", encode(classic));
    until([] { return reconciliation::view()->completed_checks.contains("Event:TresUIMobilePortalDataAsset"); });
    reconciliation::observe("runtime_space_checks.json", R"({"schema":"kh3ap.space_checks.v1","seed_name":"test","slot":1,"source":"native_space_completion","completed":["Battle1"]})");
    reconciliation::observe("runtime_ingredient_state.json", R"({"schema":"kh3ap.runtime_ingredient_state.v1","seed_name":"test","slot":1,"source":"food_prize","acquired_spots":["SPOT1"]})");
    reconciliation::observe("runtime_workshop.json", R"({"schema":"kh3ap.workshop.v1","seed_name":"test","slot":1,"source":"native_workshop_flags","completed_checks":["Synthesis:Recipe1"]})");
    until([] { return reconciliation::view()->completed_checks.contains("Synthesis:Recipe1"); });
    assert(reconciliation::view()->completed_checks.contains("GummiSpace:Battle1"));
    assert(reconciliation::view()->completed_checks.contains("IngredientSpot:SPOT1"));
    Json adversary{{"schema", "kh3ap.runtime_adversary_state.v1"}, {"seed_name", "test"}, {"slot", 1},
        {"source", "native_kill_counters"}, {"process_id", GetCurrentProcessId()},
        {"defeated", Json::array_t{"HEARTLESS_08", "NOT_IN_SEED"}}};
    adversary["seed_name"] = "wrong";
    reconciliation::observe("runtime_adversary_state.json", encode(adversary)); synchronize();
    assert(!reconciliation::view()->completed_checks.contains("Adversary:HEARTLESS_08"));
    adversary["seed_name"] = "test"; adversary["process_id"] = 0;
    reconciliation::observe("runtime_adversary_state.json", encode(adversary)); synchronize();
    assert(!reconciliation::view()->completed_checks.contains("Adversary:HEARTLESS_08"));
    adversary["process_id"] = GetCurrentProcessId();
    reconciliation::observe("runtime_adversary_state.json", encode(adversary));
    until([] { return reconciliation::view()->completed_checks.contains("Adversary:HEARTLESS_08"); });
    assert(!reconciliation::view()->completed_checks.contains("Adversary:NOT_IN_SEED"));
    const auto completed_adversaries = reconciliation::view()->completed_checks;
    reconciliation::observe("runtime_adversary_state.json", encode(adversary)); synchronize();
    assert(reconciliation::view()->completed_checks == completed_adversaries);
    adversary["defeated"] = Json::array_t{};
    reconciliation::observe("runtime_adversary_state.json", encode(adversary)); synchronize();
    assert(reconciliation::view()->completed_checks.contains("Adversary:HEARTLESS_08"));
    reconciliation::observe("runtime_context.json", R"({"hook":"RuntimeWorldContext","world":"World /Game/Levels/ss/ss_500/ss_500"})");
    synchronize(); assert(!reconciliation::view()->goal_complete);
    reconciliation::observe("runtime_context.json", R"({"hook":"RuntimeWorldContext","source":"recovered_from_ue4ss_log","world":"World /Game/Levels/ss/ss_50/ss_50"})");
    synchronize(); assert(!reconciliation::view()->goal_complete);
    reconciliation::observe("runtime_context.json", R"({"hook":"RuntimeWorldContext","world":"World /Game/Levels/ss/ss_50/ss_50"})");
    until([] { return reconciliation::view()->goal_complete; });
    const auto completed_checks = reconciliation::view()->completed_checks;
    reconciliation::stop();
    // Cold recovery preserves completed checks and goal but replays forced
    // inventory after loading a save that predates its runtime grant.
    reconciliation::start(check_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 31, ticks);
    until([] { return reconciliation::view()->ready; });
    assert(reconciliation::view()->completed_checks == completed_checks && reconciliation::view()->goal_complete);
    assert(reconciliation::view()->active.contains(0) && !reconciliation::view()->active.contains(1));
    assert(reconciliation::view()->delivery->grants.front().index == 1);
    reconciliation::stop();
    // Upgrade recovery: the old worker journal omitted the special chests
    // even though their opened state was safely checkpointed.
    const auto music_recovery_root = root / "music-recovery";
    std::filesystem::create_directory(music_recovery_root);
    std::filesystem::copy_file(check_root / "slot_data.json", music_recovery_root / "slot_data.json");
    {
        std::ifstream file(check_root / reconciliation::journal_file);
        Json old_journal;
        assert(kh3ap::json::parse(old_journal, std::string(std::istreambuf_iterator<char>(file), {})));
        for (const auto member : {"observed_checks", "completed_checks"})
        {
            auto& keys = old_journal[member].get_ref<Json::array_t&>();
            std::erase_if(keys, [](const auto& key) { return key.template get_ref<const std::string&>().find("AP_LBOX_") != std::string::npos; });
        }
        std::ofstream(music_recovery_root / reconciliation::journal_file) << encode(old_journal);
    }
    std::ofstream(music_recovery_root / "chest_progress.json") << R"({"schema":"kh3ap.chest_progress.v1","seed_name":"test","slot":1,"completed":["AP_LBOX_007","AP_LBOX_008"]})";
    reconciliation::start(music_recovery_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 32, ticks);
    until([] { return reconciliation::view()->ready; });
    for (const auto key : {"MusicChest:AP_LBOX_007", "MusicChest:AP_LBOX_008",
        "MusicChestBonus:MusicChest:AP_LBOX_007", "MusicChestBonus:MusicChest:AP_LBOX_008"})
        assert(reconciliation::view()->completed_checks.contains(key));
    reconciliation::stop();
    // Hints use decoded report tokens, never network names. Revealed reports
    // survive rollback and cold recovery before any Python ledger is consulted.
    const auto hint_root = root / "hints";
    std::filesystem::create_directory(hint_root);
    std::ofstream(hint_root / "slot_data.json") << R"({"kh3_seed":{"seed_name":"test","slot":1}})";
    std::ofstream(hint_root / "runtime_seed_hints.json") << R"({"schema":"kh3ap.seed_hints.v1","seed_name":"test","slot":1,
        "reports":{"1":[{"player":2,"location":20},{"player":2,"location":20}]}})";
    input["items"] = Json::array_t{
        Json{{"index", 0}, {"item_id", 1263022520}, {"location_id", -1}, {"from_player", 2}, {"item_name", "misleading display"}},
        Json{{"index", 1}, {"item_id", 1263022524}, {"location_id", -1}, {"from_player", 2}},
        Json{{"index", 2}, {"item_id", -999}, {"location_id", -1}, {"from_player", 2}, {"item_name", "Secret Report 5"}}};
    reconciliation::start(hint_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 40, ticks);
    until([] { return reconciliation::view()->ready; });
    {
        std::lock_guard lock{test_mutex};
        assert(published.at("runtime_secret_report_hints.json")["reports"].get_ref<const Json::object_t&>().empty());
    }
    const int64_t reports_applied[] = {0, 1, 2};
    complete_receipts(reports_applied, reconciliation::view()->epoch);
    until([] { std::lock_guard lock{test_mutex}; return published.at("runtime_secret_report_hints.json")["reports"].contains("1"); });
    {
        std::lock_guard lock{test_mutex};
        const auto& hints = published.at("runtime_secret_report_hints.json");
        assert(hints["reports"]["1"].get_ref<const Json::array_t&>().size() == 1);
        assert(!hints["reports"].contains("5"));
        assert(hints["missing_report_numbers"].get_ref<const Json::array_t&>() == Json::array_t{Json(13)});
    }
    const auto hint_epoch = reconciliation::view()->epoch;
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 41, ticks);
    until([&] { return reconciliation::view()->epoch > hint_epoch && reconciliation::view()->ready; });
    assert(!reconciliation::view()->active.contains(0));
    {
        std::lock_guard lock{test_mutex};
        assert(published.at("runtime_secret_report_hints.json")["reports"].contains("1"));
        assert(published.at("item_delivery_ledger_summary.json")["applied_count"].get<double>() == 3);
        assert(published.at("item_delivery.json")["applied_item_indices"].get_ref<const Json::array_t&>().empty());
        assert(published.at("item_consumer_plan.json")["pending_action_count"].get<double>() == 2);
    }
    reconciliation::stop();
    reconciliation::start(hint_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 42, ticks);
    until([] { return reconciliation::view()->ready; });
    {
        std::lock_guard lock{test_mutex};
        assert(published.at("runtime_secret_report_hints.json")["reports"].contains("1"));
        assert(published.at("runtime_secret_report_hints.json")["missing_report_numbers"].get_ref<const Json::array_t&>() == Json::array_t{Json(13)});
    }
    reconciliation::stop();
    // A failed journal commit cannot advertise a newly revealed report.
    const auto failed_hint_root = root / "failed-hints";
    std::filesystem::create_directory(failed_hint_root);
    std::filesystem::copy_file(hint_root / "slot_data.json", failed_hint_root / "slot_data.json");
    std::filesystem::copy_file(hint_root / "runtime_seed_hints.json", failed_hint_root / "runtime_seed_hints.json");
    reconciliation::start(failed_hint_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 43, ticks);
    until([] { return reconciliation::view()->ready; });
    std::filesystem::remove(failed_hint_root / reconciliation::journal_file);
    std::filesystem::create_directory(failed_hint_root / reconciliation::journal_file);
    { std::lock_guard lock{test_mutex}; published.erase("runtime_receipt_ack_event.json"); }
    completed_ack["delivery_epoch"] = reconciliation::view()->epoch;
    completed_ack["applied_indices"] = Json::array_t{0, 1};
    reconciliation::observe("runtime_item_apply_event.json", encode(completed_ack));
    until([] { return reconciliation::view()->error == "journal_commit_failed"; });
    until([] { std::lock_guard lock{test_mutex}; return published.at("runtime_secret_report_hints.json")["error"].get_ref<const std::string&>() == "journal_commit_failed"; });
    {
        std::lock_guard lock{test_mutex};
        assert(published.at("runtime_secret_report_hints.json")["reports"].get_ref<const Json::object_t&>().empty());
        assert(!published.contains("runtime_receipt_ack_event.json"));
        assert(published.at("item_delivery_ledger_summary.json")["applied_count"].get<double>() == 0);
    }
    reconciliation::stop();
    // Multiple queued frame completions must all be forwarded in order after
    // native accounting commits, even when the worker publishes them together.
    const auto batch_root = root / "ack-batches";
    std::filesystem::create_directory(batch_root);
    std::ofstream(batch_root / "slot_data.json") << R"({"kh3_seed":{"seed_name":"test","slot":1}})";
    Json batch_input{{"schema", "kh3ap.receipt_catalog.v4"}, {"process_id", GetCurrentProcessId()},
        {"seed_name", "test"}, {"slot", 1}, {"server_checked_locations", Json::array_t{}}, {"items", Json::array_t{
            Json{{"index", 0}, {"item_id", 1263022577}, {"from_player", 2}, {"location_id", -1}},
            Json{{"index", 1}, {"item_id", 1263022577}, {"from_player", 2}, {"location_id", -1}},
            Json{{"index", 2}, {"item_id", 1263022577}, {"from_player", 2}, {"location_id", -1}}}}};
    reconciliation::start(batch_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(batch_input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 44, ticks);
    until([] { return reconciliation::view()->ready; });
    {
        std::lock_guard lock{test_mutex};
        receipt_events.clear();
        for (int index = 0; index < 3; ++index)
        {
            completed_ack["delivery_epoch"] = reconciliation::view()->epoch;
            completed_ack["request_id"] = "batch-" + std::to_string(index);
            completed_ack["applied_indices"] = Json::array_t{index};
            reconciliation::observe("runtime_item_apply_event.json", encode(completed_ack));
        }
    }
    until([] { std::lock_guard lock{test_mutex}; return receipt_events.size() == 3; });
    {
        std::lock_guard lock{test_mutex};
        for (int index = 0; index < 3; ++index)
            assert(receipt_events[index]["indices"].get_ref<const Json::array_t&>().front().get<double>() == index);
        assert(published.at("item_delivery_ledger_summary.json")["applied_count"].get<double>() == 3);
    }
    reconciliation::stop();
    // A multiworld's seed data can exceed 4 MiB even without whitespace.
    // The shared reader accepts large settings as well as a large location catalog.
    const auto large_root = root / "large-seed";
    std::filesystem::create_directory(large_root);
    const auto seed_data = Json{{"kh3_seed", Json{{"seed_name", "test"}, {"slot", 1}}},
        {"bridge", Json{{"locations", Json::array_t{Json{{"ap_location_id", 200},
            {"bridge_key", "TreasureTS:TS_SBOX_002"}, {"source_id", "TS_SBOX_002"},
            {"source_table", "TreasureTS"}, {"type", "treasure"}, {"is_foreign_item", true}}}}}},
        {"padding", std::string(5 * 1024 * 1024, 'x')}};
    std::ofstream(large_root / "slot_data.json", std::ios::binary) << encode(seed_data);
    std::ofstream(large_root / "runtime_seed_settings.json", std::ios::binary) << encode(Json{
        {"schema", "kh3ap.seed_settings.v1"}, {"slot_data", seed_data}});
    assert(json::read_seed_document(large_root / "runtime_seed_settings.json")->value["slot_data"] == seed_data);
    std::ofstream(large_root / "chest_progress.json") << R"({"schema":"kh3ap.chest_progress.v1","seed_name":"test","slot":1,"completed":["TS_SBOX_002"]})";
    reconciliation::start(large_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(batch_input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 45, ticks);
    until([] { return reconciliation::view()->ready; });
    assert(reconciliation::view()->completed_checks.contains("TreasureTS:TS_SBOX_002"));
    assert(reconciliation::view()->delivery && reconciliation::view()->delivery->grants.size() == 3);
    until([] { std::lock_guard lock{test_mutex};
        return published.at("runtime_check_state.json")["ready"].get<bool>() &&
            published.at("runtime_check_state.json")["checked_locations"] == Json::array_t{"TreasureTS:TS_SBOX_002"}; });
    // Ordinary channel payloads retain their 4 MiB limit.
    reconciliation::observe("runtime_receipt_catalog.json", encode(batch_input) + std::string(4 * 1024 * 1024, ' '));
    until([] { return reconciliation::view()->error == "json_size_limit"; });
    reconciliation::stop();
    // Optional hint corruption cannot stop core checks or receipt delivery.
    for (const std::string condition : {"missing", "malformed", "wrong-seed", "oversize"})
    {
        const auto bad_root = root / ("hint-" + condition);
        std::filesystem::create_directory(bad_root);
        auto small_seed = seed_data; small_seed.erase("padding");
        std::ofstream(bad_root / "slot_data.json") << encode(small_seed);
        std::filesystem::copy_file(large_root / "chest_progress.json", bad_root / "chest_progress.json");
        const auto hints_path = bad_root / "runtime_seed_hints.json";
        if (condition == "malformed") std::ofstream(hints_path) << "{";
        if (condition == "wrong-seed") std::ofstream(hints_path) <<
            R"({"schema":"kh3ap.seed_hints.v1","seed_name":"other","slot":1,"reports":{}})";
        if (condition == "oversize") std::ofstream(hints_path, std::ios::binary) <<
            std::string(json::limits::hints_bytes + 1, ' ');
        reconciliation::start(bad_root);
        reconciliation::observe("runtime_receipt_catalog.json", encode(batch_input));
        reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 46, ticks);
        until([] { return reconciliation::view()->ready; });
        assert(reconciliation::view()->error.empty());
        assert(reconciliation::view()->completed_checks.contains("TreasureTS:TS_SBOX_002"));
        assert(reconciliation::view()->delivery && reconciliation::view()->delivery->grants.size() == 3);
        until([] { std::lock_guard lock{test_mutex};
            return !published.at("runtime_secret_report_hints.json")["error"].get_ref<const std::string&>().empty(); });
        reconciliation::stop();
    }
    // All seed readers use one boundary policy and immutable parsed cache.
    for (const auto* filename : {"slot_data.json", "runtime_seed_settings.json", "runtime_seed_hints.json"})
    {
        const auto limit_root = root / (std::string("limit-") + filename);
        std::filesystem::create_directory(limit_root);
        const auto path = limit_root / filename;
        Json value{{"kh3_seed", Json{{"seed_name", "test"}, {"slot", 1}}}};
        if (std::string_view(filename) != "slot_data.json") value["schema"] =
            std::string_view(filename) == "runtime_seed_hints.json" ? "kh3ap.seed_hints.v1" : "kh3ap.seed_settings.v1";
        auto padded = encode(value);
        padded.resize(std::string_view(filename) == "runtime_seed_hints.json" ?
            json::limits::hints_bytes : json::limits::seed_bytes, ' ');
        std::ofstream(path, std::ios::binary) << padded;
        const auto document = json::read_seed_document(path);
        assert(document->value == value);
        assert(json::read_seed_document(path) == document);
        assert(json::read_seed_json(path).get() == &document->value);
        std::ofstream(path, std::ios::binary | std::ios::app).put(' ');
        bool rejected = false;
        try { json::read_seed_document(path); }
        catch (const std::runtime_error& e) { rejected = e.what() == std::string("seed_metadata_size_limit:") + filename; }
        assert(rejected);
        // Same-size atomic replacement invalidates the cache by modification time.
        std::ofstream(path, std::ios::binary) << encode(value);
        const auto before = json::read_seed_document(path);
        auto replacement = value; replacement["kh3_seed"]["slot"] = 2;
        const auto modified = std::filesystem::last_write_time(path);
        std::ofstream(path, std::ios::binary) << encode(replacement);
        std::filesystem::last_write_time(path, modified + std::chrono::seconds(1));
        assert(json::read_seed_document(path)->value == replacement);
        assert(before->value == value);
        std::filesystem::remove(path);
        rejected = false;
        try { json::read_seed_document(path); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    }
    // A context-only publication reuses receipt/save projections. Changes in
    // acknowledgment history, readiness, catalog facts and save membership must
    // still invalidate them, including an ACK with unchanged active membership.
    const auto projection_root = root / "projection-cache";
    std::filesystem::create_directory(projection_root);
    std::ofstream(projection_root / "slot_data.json") << chest_fixture::seed({}).dump();
    { std::lock_guard lock{test_mutex}; published.clear(); publication_counts.clear(); }
    reconciliation::start(projection_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(batch_input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 80, ticks);
    until([] { std::lock_guard lock{test_mutex}; return published.contains("item_delivery.json") &&
        published.at("item_delivery.json")["native_delivery_ready"] == true; });
    Json projection_room{{"hook", "RuntimePlayablePawnContext"}, {"object", "Sora_Projection"},
        {"world", "/Game/Levels/tt/tt_01"}, {"source", "test"}, {"opened_at", "2026-10-17T12:00:00Z"}};
    auto context_round = [&](int serial) {
        projection_room["test_serial"] = serial;
        reconciliation::observe("runtime_context.json", encode(projection_room));
        until([&] { std::lock_guard lock{test_mutex}; return published.contains("runtime_reconciliation_state.json") &&
            published.at("runtime_reconciliation_state.json")["runtime_context"]["test_serial"] == serial; });
    };
    context_round(0);
    size_t prior_delivery{}, prior_saves{};
    { std::lock_guard lock{test_mutex}; prior_delivery = publication_counts["item_delivery.json"];
        prior_saves = publication_counts["runtime_save_receipts.json"]; }
    context_round(1);
    { std::lock_guard lock{test_mutex};
        assert(publication_counts["item_delivery.json"] == prior_delivery);
        assert(publication_counts["runtime_save_receipts.json"] == prior_saves); }
    const std::array<int64_t, 1> one_receipt{0};
    complete_receipts(one_receipt, reconciliation::view()->epoch);
    until([] { std::lock_guard lock{test_mutex}; return published.at("item_delivery_ledger_summary.json")["applied_count"] == 1; });
    reconciliation::file_open(L"synthetic/KHIII_slot50.bin", GENERIC_WRITE, 81, ticks);
    reconciliation::file_closed(81, true);
    until([] { std::lock_guard lock{test_mutex}; return published.at("runtime_save_receipts.json")["slots"]
        .contains("kh3sv2/data/KHIII_slot50.bin"); });
    { std::lock_guard lock{test_mutex}; assert(published.at("runtime_save_receipts.json")["slots"]
        ["kh3sv2/data/KHIII_slot50.bin"]["saved_receipts"]["through"] == 0); }
    std::string prior_ack;
    { std::lock_guard lock{test_mutex}; prior_ack = published.at("runtime_item_apply_state.json")["last_processed_event_key"].get<std::string>(); }
    complete_receipts(one_receipt, reconciliation::view()->epoch);
    until([&] { std::lock_guard lock{test_mutex}; return published.at("runtime_item_apply_state.json")["last_processed_event_key"] != prior_ack; });
    reconciliation::observe("current_state_write_polyhook_events.jsonl", R"({"state_class":"TresPlayerState_GameOver","state_comp_matches_cached_pawn":true})");
    until([] { std::lock_guard lock{test_mutex}; return published.at("item_delivery.json")["native_delivery_ready"] == false; });
    { std::lock_guard lock{test_mutex}; assert(published.at("item_delivery.json")["pending_items"].size() == 2); }
    projection_room["object"] = "Sora_Projection_Retry";
    context_round(2);
    until([] { std::lock_guard lock{test_mutex}; return published.at("item_delivery.json")["native_delivery_ready"] == true; });
    auto renamed_catalog = batch_input;
    renamed_catalog["items"][0]["location_name"] = "Updated location";
    reconciliation::observe("runtime_receipt_catalog.json", encode(renamed_catalog));
    until([] { std::lock_guard lock{test_mutex}; return published.at("runtime_item_descriptions.json")["items"]["0"]["location_name"] == "Updated location"; });
    // Repeated identical catalog facts are also safe after the cache was populated.
    reconciliation::observe("runtime_receipt_catalog.json", encode(renamed_catalog));
    context_round(3);
    { std::lock_guard lock{test_mutex};
        assert(published.at("item_delivery_ledger.json")["items"]["0"]["location_name"] == "Updated location"); }
    for (const auto& name : {"item_delivery_ledger.json", "item_delivery.json", "item_delivery_state.json",
            "item_consumer_plan.json", "runtime_item_apply_state.json", "runtime_item_grant_state.json",
            "item_delivery_ledger_summary.json"})
        assert(!std::filesystem::exists(projection_root / name));
    reconciliation::observe("runtime_receipt_catalog.json", "{invalid}");
    until([] { std::lock_guard lock{test_mutex}; return published.at("item_delivery.json")["native_delivery_ready"] == false &&
        !published.at("runtime_save_receipts.json")["error"].get<std::string>().empty(); });
    reconciliation::stop();
    // KH0.2 completion uses the same worker, durable check journal and receipt
    // scheduler. These synthetic callbacks never inspect or create game saves.
    const auto kh02_root = root / "kh02-checks";
    std::filesystem::create_directory(kh02_root);
    const Json chest_check{{"type", "kh02_chest"}, {"source_table", "KH02"},
        {"source_id", "Treasure:DW_BOX003"}, {"bridge_key", "KH02:Treasure:DW_BOX003"},
        {"ap_location_id", 1263350786}, {"ap_location_name", "0.2 chest"},
        {"kh02_flag", "gameflow_DW_CHALLENGE_CHEST_DW_BOX003"}, {"bridge_delivery_required", true}};
    const Json boss_check{{"type", "kh02_boss"}, {"source_table", "KH02"},
        {"source_id", "Boss:CastleDemonTower"}, {"bridge_key", "KH02:Boss:CastleDemonTower"},
        {"ap_location_id", 1263351041}, {"ap_location_name", "0.2 boss"},
        {"kh02_flag", "BattleBonus_DW_dw_02_DevilTower"}, {"bridge_delivery_required", true},
        {"is_foreign_item", true}, {"assigned_item_name", "Potion"}, {"assigned_item_player_name", "Partner"}};
    const Json challenge_check{{"type", "kh02_objective"}, {"source_table", "KH02"},
        {"source_id", "Objective:023"}, {"bridge_key", "KH02:Objective:023"},
        {"ap_location_id", 1263350551}, {"ap_location_name", "0.2 lamps"},
        {"kh02_challenge", 22}, {"bridge_delivery_required", true}};
    const Json aqua_check{{"type", "character_unlock"}, {"source_table", "KH02"},
        {"source_id", "Boss:UnlockAqua"}, {"bridge_key", "KH02:Boss:UnlockAqua"},
        {"ap_location_id", 1263351298}, {"ap_location_name", "0.2 - Unlock Aqua"},
        {"kh02_flag", "BattleBonus_DW_dw_10_DevilWave"}, {"bridge_delivery_required", true}};
    assert(!kh02_checks::condition(converted));
    auto invalid_chest = chest_check;
    invalid_chest["kh02_flag"] = "gameflow_DW_VISITED_2";
    bool rejected_condition = false;
    try { kh02_checks::condition(invalid_chest); }
    catch (const std::exception&) { rejected_condition = true; }
    assert(rejected_condition);
    assert(kh02_checks::second_darkside_victory(
        L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.dw_08_gameflow_C:ExecuteUbergraph_dw_08_gameflow",
        L"gameflow_dw", 340, 341));
    assert(!kh02_checks::second_darkside_victory(L"/Game/Maps/APKH02Mirrors/APKH02Forest",
        L"gameflow_dw", 340, 341));
    assert(!kh02_checks::second_darkside_victory(
        L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.dw_08_gameflow_C:ExecuteUbergraph_dw_08_gameflow",
        L"gameflow_dw", 810, 341));
    std::ofstream(kh02_root / "slot_data.json") << encode(chest_fixture::seed({chest_check, boss_check, challenge_check, aqua_check}));
    reconciliation::start(kh02_root);
    until([] { return reconciliation::view()->chests != nullptr; });
    assert(reconciliation::view()->kh02_ap_chests == std::set<std::wstring>{L"DW_BOX003"});
    Json kh02_catalog{{"schema", "kh3ap.receipt_catalog.v4"}, {"process_id", GetCurrentProcessId()},
        {"seed_name", "test"}, {"slot", 1}, {"items", Json::array_t{}}, {"server_checked_locations", Json::array_t{}}};
    reconciliation::observe("runtime_receipt_catalog.json", encode(kh02_catalog));
    reconciliation::file_open(L"synthetic/KHIII_slot99.bin", GENERIC_READ, 90, ticks);
    until([] { return reconciliation::view()->ready; });
    const auto kh02_revision = reconciliation::view()->kh02_progress_revision;
    Json progress{{"schema", "kh02.mirror_progress.v1"}, {"flags", Json{{"gameflow_DW", 810}}},
        {"challenges", Json::object()}, {"counts", Json::object()}, {"events", Json::object()}};
    auto cleared_progress = progress;
    cleared_progress["flags"]["gameflow_DW_CHALLENGE_CHEST_DW_BOX003"] = 1;
    cleared_progress["flags"]["BattleBonus_DW_dw_02_DevilTower"] = 1;
    cleared_progress["challenges"]["22"] = 2;
    reconciliation::kh02_progress_update(*kh02_progress::from_json(cleared_progress), kh02_revision, L"wrong-seed", 1);
    reconciliation::kh02_progress_update(*kh02_progress::from_json(cleared_progress), kh02_revision, L"test", 2);
    reconciliation::kh02_progress_update(*kh02_progress::from_json(cleared_progress), kh02_revision + 1, L"test", 1);
    // A matching empty update is an ordered barrier after the rejected requests.
    reconciliation::kh02_progress_update(*kh02_progress::from_json(progress), kh02_revision, L"test", 1);
    until([&] { return kh02_progress::to_json(reconciliation::view()->kh02_progress) == progress; });
    assert(reconciliation::view()->completed_checks.empty());
    progress["challenges"]["22"] = 1;
    progress["challenges"]["41"] = 2; // Unselected Boss Rush never becomes an AP check.
    reconciliation::kh02_progress_update(*kh02_progress::from_json(progress), kh02_revision, L"test", 1);
    until([&] { return kh02_progress::to_json(reconciliation::view()->kh02_progress) == progress; });
    assert(reconciliation::view()->completed_checks.empty());
    size_t notifications_before;
    { std::lock_guard lock{test_mutex}; notifications_before = notifications.size(); }
    reconciliation::kh02_progress_update(*kh02_progress::from_json(cleared_progress), kh02_revision, L"test", 1);
    until([] { return reconciliation::view()->completed_checks.size() == 3; });
    auto kh02_completed = reconciliation::view()->completed_checks;
    assert(kh02_completed == std::set<std::string>({"KH02:Treasure:DW_BOX003",
        "KH02:Boss:CastleDemonTower", "KH02:Objective:023"}));
    reconciliation::kh02_progress_update(*kh02_progress::from_json(cleared_progress), kh02_revision, L"test", 1);
    reconciliation::kh02_progress_update(*kh02_progress::from_json(progress), kh02_revision, L"test", 1);
    until([&] { return kh02_progress::to_json(reconciliation::view()->kh02_progress) == progress; });
    assert(reconciliation::view()->completed_checks == kh02_completed);
    { std::lock_guard lock{test_mutex}; assert(notifications.size() == notifications_before + 1); }
    // The recovery command's final checkpoint produces the same Aqua check as
    // the final story victory, with no direct inventory grant or fake receipt.
    const auto finished_chapter = kh02_gameflow::update(progress, "gameflow_DW", 9999);
    reconciliation::kh02_progress_update(*kh02_progress::from_json(finished_chapter), kh02_revision, L"test", 1);
    until([] { return reconciliation::view()->completed_checks.contains("KH02:Boss:UnlockAqua"); });
    kh02_completed.insert("KH02:Boss:UnlockAqua");
    assert(reconciliation::view()->completed_checks == kh02_completed);
    assert(!reconciliation::view()->delivery);
    // A local reward still needs its genuine receipt and native grant ACK;
    // opening the AP chest must never mark that reward as already delivered.
    kh02_catalog["items"] = Json::array_t{Json{{"index", 0}, {"item_id", 1263022577},
        {"from_player", 1}, {"location_id", 1263350786}},
        Json{{"index", 1}, {"item_id", 1263022656}, {"from_player", 1}, {"location_id", 1263351298}}};
    reconciliation::observe("runtime_receipt_catalog.json", encode(kh02_catalog));
    until([] { return reconciliation::view()->delivery != nullptr; });
    assert(!reconciliation::view()->active.contains(0));
    assert(reconciliation::view()->delivery->grants.size() == 2);
    const auto aqua_delivery = reconciliation::view()->delivery;
    const auto& aqua_grant = aqua_delivery->grants.at(1);
    assert(aqua_grant.index == 1 && aqua_grant.grant_method == L"character_unlock" &&
        aqua_grant.runtime_token == L"KH3AP_CHARACTER_UNLOCK::10");
    const int64_t aqua_receipt[] = {1};
    complete_receipts(aqua_receipt, reconciliation::view()->epoch);
    until([] { return reconciliation::view()->active.contains(1); });
    reconciliation::kh02_progress_update(*kh02_progress::from_json(finished_chapter), kh02_revision, L"test", 1);
    reconciliation::observe("runtime_receipt_catalog.json", encode(kh02_catalog));
    until([] { return reconciliation::view()->delivery && reconciliation::view()->delivery->grants.size() == 1; });
    assert(reconciliation::view()->delivery->grants.front().index == 0);
    assert(reconciliation::view()->completed_checks == kh02_completed);
    reconciliation::kh02_progress_reset(kh02_revision + 1, L"test", 1);
    reconciliation::kh02_progress_reset(kh02_revision, L"wrong-seed", 1);
    reconciliation::kh02_progress_reset(kh02_revision, L"test", 1);
    reconciliation::kh02_progress_reset(kh02_revision, L"test", 1);
    until([&] { return reconciliation::view()->kh02_progress_revision == kh02_revision + 1; });
    assert(!reconciliation::view()->kh02_progress);
    assert(reconciliation::view()->completed_checks == kh02_completed);
    assert(!reconciliation::view()->active.contains(0));
    reconciliation::kh02_progress_update(*kh02_progress::from_json(cleared_progress), kh02_revision, L"test", 1);
    reconciliation::stop();
    reconciliation::start(kh02_root);
    until([&] { return reconciliation::view()->completed_checks == kh02_completed; });
    assert(reconciliation::view()->error.empty());
    reconciliation::stop();
    assert(std::filesystem::canonical(root).parent_path() == parent);
    // Native in-memory backup/restore is separate from disk slots and room
    // entry. A replaced same-room checkpoint includes sparse receipt 2, while
    // the grant after that checkpoint (1) must replay exactly once.
    const auto snapshot_root=root/"native-snapshot";
    std::filesystem::create_directory(snapshot_root);
    const auto keyblade_source=chest_fixture::vanilla(1);
    Json keyblade_chest{{"type","treasure"},{"source_id",keyblade_source["source_id"]},
        {"source_table",keyblade_source["source_table"]},{"ap_location_id",keyblade_source["ap_location_id"]},
        {"keyblade_chest_keyblade","Hunny Spout"},{"keyblade_chest_token","WEP_KEYBLADE_SO_07"},
        {"keyblade_chest_native",false}};
    auto snapshot_seed=chest_fixture::seed({keyblade_chest,placed});
    snapshot_seed["chestsanity"]=1;
    snapshot_seed["bridge"]["locations"].get_ref<Json::array_t&>().push_back(Json{
        {"ap_location_id",90000},{"bridge_key","Event:MIXED"},{"source_table","Event"},
        {"source_id","MIXED"},{"bridge_delivery_required",true}});
    std::ofstream(snapshot_root/"slot_data.json") << snapshot_seed.dump();
    embed_bundle(snapshot_root, R"({"bridge_key":"Event:MIXED","randomized_tokens":["ETresAbilityKind::AERO_UP","ETresVictoryBonusKind::MELEM_FIRE","ITEM_POTION"],"vanilla_tokens":[]})");
    reconciliation::inventory_snapshot_observer(true);
    reconciliation::start(snapshot_root);
    auto snapshot_input=batch_input;
    snapshot_input["items"].get_ref<Json::array_t&>().push_back(Json{{"index",3},{"item_id",1263022201},{"from_player",2},{"location_id",-1}});
    snapshot_input["items"].get_ref<Json::array_t&>().push_back(Json{{"index",4},{"item_id",1263022503},{"from_player",2},{"location_id",-1}});
    snapshot_input["items"].get_ref<Json::array_t&>().push_back(Json{{"index",5},{"item_id",1263022449},{"from_player",2},{"location_id",-1}});
    snapshot_input["items"].get_ref<Json::array_t&>().push_back(Json{{"index",6},{"item_id",1263022201},{"from_player",1},{"location_id",90000}});
    reconciliation::observe("runtime_receipt_catalog.json",encode(snapshot_input));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin",GENERIC_READ,90,ticks);
    until([] { return reconciliation::view()->ready; });
    reconciliation::observe("runtime_context.json",encode(projection_room));
    complete_receipts(std::array<int64_t,1>{0},reconciliation::view()->epoch);
    reconciliation::inventory_snapshot(false,ticks);
    complete_receipts(std::array<int64_t,1>{2},reconciliation::view()->epoch);
    reconciliation::inventory_snapshot(false,ticks+1);
    complete_receipts(std::array<int64_t,1>{1},reconciliation::view()->epoch);
    complete_receipts(std::array<int64_t,1>{3},reconciliation::view()->epoch); // Aero Boost writes both banks
    complete_receipts(std::array<int64_t,2>{4,5},reconciliation::view()->epoch); // Potion persists; Fire replays
    complete_receipts(std::array<int64_t,1>{6},reconciliation::view()->epoch); // mixed Aero Boost/Fire/Potion bundle
    until([] { return reconciliation::view()->active.contains(6); });
    until([] { return reconciliation::view()->active.contains(5); });
    until([] { return reconciliation::view()->active.contains(1); });
    const auto before_restore_epoch=reconciliation::view()->epoch;
    const auto before_death_chests=reconciliation::view()->chests;
    assert(before_death_chests && before_death_chests->assignments.size()==2);
    reconciliation::observe("current_state_write_polyhook_events.jsonl",
        R"({"state_class":"TresPlayerState_GameOver","state_comp_matches_cached_pawn":true})");
    until([] { return !reconciliation::view()->ready; });
    // A delayed map-name callback can arrive on the death screen, before
    // either in-memory Retry restoration or an explicit Continue load.
    // Neither that callback nor a newly discovered pawn proves restoration.
    auto late_ready=projection_room;
    late_ready["schema"]="kh3ap.runtime_map_ready.v1";
    late_ready["source"]="MapName:UIPMapNameCallback:cached_pawn";
    for (int callback=0;callback<3;++callback)
    {
        late_ready["test_serial"]=100+callback;
        if(callback==1) late_ready["object"]="Sora_BeforeInventoryRestore";
        if(callback==2) late_ready["pawn_source"]="VanillaLevelWritePolyhook:level_owner";
        reconciliation::observe(callback==2?"runtime_context.json":"runtime_map_ready.json",encode(late_ready));
        until([&] {
            std::lock_guard lock{test_mutex};
            return published.at("runtime_reconciliation_state.json")["runtime_context"]["test_serial"]==100+callback;
        });
        const auto waiting=reconciliation::view();
        assert(waiting->error.empty() && !waiting->ready && !waiting->delivery);
        assert(waiting->active.contains(1) && waiting->active.contains(6));
        assert(waiting->chests==before_death_chests);
        std::lock_guard lock{test_mutex};
        assert(published.contains("runtime_music_chests.json") && published.contains("runtime_keyblade_chests.json"));
    }
    reconciliation::inventory_snapshot(true,ticks+2);
    until([] { return !reconciliation::view()->ready && !reconciliation::view()->active.contains(1); });
    assert(reconciliation::view()->active.contains(0) && reconciliation::view()->active.contains(2) &&
        reconciliation::view()->active.contains(3));
    assert(reconciliation::view()->active.contains(4) && !reconciliation::view()->active.contains(5));
    assert(!reconciliation::view()->active.contains(6));
    // Cached context is not proof of completed restoration. A map-ready event
    // may reuse Sora's identity; it must not replace the native checkpoint.
    projection_room["pawn_source"]="VanillaLevelWritePolyhook:level_owner";
    reconciliation::observe("runtime_context.json",encode(projection_room));
    until([] { return reconciliation::view()->ready; });
    const auto mixed_only_missing=[] {
        const auto batch=reconciliation::view()->delivery;
        if (!batch) return false;
        std::vector<std::wstring> tokens;
        for (const auto& grant:batch->grants) if (grant.index==6) tokens.push_back(grant.runtime_token);
        return tokens==std::vector<std::wstring>{L"ETresVictoryBonusKind::MELEM_FIRE"};
    };
    assert(mixed_only_missing());
    complete_receipts(std::array<int64_t,1>{1},before_restore_epoch); // stale ACK
    const auto first_restore_epoch=reconciliation::view()->epoch;
    reconciliation::inventory_snapshot(true,ticks+3);
    until([&] { return reconciliation::view()->epoch>first_restore_epoch; });
    assert(!reconciliation::view()->active.contains(1) && reconciliation::view()->active.contains(2) &&
        reconciliation::view()->active.contains(3));
    reconciliation::observe("runtime_context.json",encode(projection_room));
    until([] { return reconciliation::view()->ready; });
    assert(mixed_only_missing()); // repeated Retry cannot replay retained bundle parts
    complete_receipts(std::array<int64_t,1>{6},reconciliation::view()->epoch);
    until([] { return reconciliation::view()->active.contains(6); });
    // Loading/Continue from a disk slot must select its own membership, even
    // when a newer in-memory checkpoint existed immediately before the load.
    reconciliation::observe("current_state_write_polyhook_events.jsonl",
        R"({"state_class":"TresPlayerState_GameOver","state_comp_matches_cached_pawn":true})");
    until([] { return !reconciliation::view()->ready; });
    late_ready["test_serial"]=103;
    reconciliation::observe("runtime_map_ready.json",encode(late_ready));
    until([&] {
        std::lock_guard lock{test_mutex};
        return published.at("runtime_reconciliation_state.json")["runtime_context"]["test_serial"]==103;
    });
    assert(!reconciliation::view()->ready && reconciliation::view()->error.empty());
    assert(reconciliation::view()->chests==before_death_chests);
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin",GENERIC_READ,91,ticks+4);
    until([] { return reconciliation::view()->ready && !reconciliation::view()->active.contains(0); });
    assert(!reconciliation::view()->active.contains(2));
    assert(!reconciliation::view()->active.contains(3) && !reconciliation::view()->active.contains(4));
    until([] { const auto batch=reconciliation::view()->delivery;
        return batch && std::count_if(batch->grants.begin(),batch->grants.end(),[](const auto& grant) { return grant.index==6; })==3; });
    reconciliation::stop();
    reconciliation::inventory_snapshot_observer(false);
    // Roxas requires the KG finish and both Keyblade receipts. Every order,
    // including a restart with two requirements met, must unlock exactly once.
    std::array roxas_order{0, 1, 2};
    int roxas_case = 0;
    const std::string roxas_key = "CharacterUnlock:Roxas";
    const std::string kg_finish = "VBonus:Vbonus_075";
    do
    {
        const auto roxas_root = root / ("roxas-order-" + std::to_string(roxas_case++));
        std::filesystem::create_directory(roxas_root);
        const Json required_blades = Json::array({1263022494, 1263022493});
        auto roxas_seed = chest_fixture::seed({
            Json{{"bridge_key", kg_finish}, {"source_table", "VBonus"}, {"source_id", "Vbonus_075"},
                 {"type", "vbonus"}, {"ap_location_id", 100}},
            Json{{"bridge_key", roxas_key}, {"source_table", "CharacterUnlock"}, {"source_id", "Roxas"},
                 {"type", "character_unlock"}, {"ap_location_id", 1263351297},
                 {"trigger_bridge_key", kg_finish}, {"required_item_ids", required_blades},
                 {"bridge_delivery_required", true}}});
        roxas_seed["bridge"]["conditional_checks"] = Json::array({
            Json{{"trigger_bridge_key", kg_finish}, {"ap_location_id", 1263351297},
                 {"required_item_ids", required_blades}}});
        std::ofstream(roxas_root / "slot_data.json") << encode(roxas_seed);
        auto roxas_catalog = input;
        roxas_catalog["items"] = Json::array();
        roxas_catalog["server_checked_locations"] = Json::array();
        reconciliation::start(roxas_root);
        reconciliation::observe("runtime_receipt_catalog.json", encode(roxas_catalog));
        until([] { return reconciliation::view()->expanded != nullptr; });
        for (int step = 0; step < 3; ++step)
        {
            const auto requirement = roxas_order[step];
            if (requirement == 0)
            {
                reconciliation::observe("runtime_vbonus_checks.json", encode(Json{
                    {"schema", "kh3ap.vbonus_progress.v1"}, {"seed_name", "test"}, {"slot", 1},
                    {"completed", Json::array({"Vbonus_075"})}}));
                until([&] { return reconciliation::view()->completed_checks.contains(kg_finish); });
            }
            else
            {
                roxas_catalog["items"].push_back(Json{{"index", requirement - 1},
                    {"item_id", requirement == 1 ? 1263022494 : 1263022493},
                    {"from_player", 2}, {"location_id", -1}});
                const auto before_catalog = reconciliation::view()->expanded;
                reconciliation::observe("runtime_receipt_catalog.json", encode(roxas_catalog));
                until([&] { return reconciliation::view()->expanded != before_catalog; });
            }
            assert(reconciliation::view()->completed_checks.contains(roxas_key) == (step == 2));
            if (step == 1)
            {
                reconciliation::stop();
                reconciliation::start(roxas_root);
                reconciliation::observe("runtime_receipt_catalog.json", encode(roxas_catalog));
                until([] { return reconciliation::view()->expanded != nullptr; });
                assert(!reconciliation::view()->completed_checks.contains(roxas_key));
            }
        }
        // Completion earns a check; the real AP receipt still owns delivery.
        assert(!reconciliation::view()->delivery);
        roxas_catalog["items"].push_back(Json{{"index", 2}, {"item_id", 1263022655},
            {"from_player", 1}, {"location_id", 1263351297}});
        reconciliation::observe("runtime_receipt_catalog.json", encode(roxas_catalog));
        reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 200 + roxas_case, ticks + 6);
        until([] { const auto batch = reconciliation::view()->delivery;
            return batch && std::any_of(batch->grants.begin(), batch->grants.end(), [](const auto& grant) {
                return grant.runtime_token == L"KH3AP_CHARACTER_UNLOCK::17";
            }); });
        assert(reconciliation::view()->error.empty());
        reconciliation::stop();
    } while (std::next_permutation(roxas_order.begin(), roxas_order.end()));
    // Pak-backed recovery and foreign rewards work with no loose catalog.
    pak_seed_fixture = json::read_seed_document(loot_root / "slot_data.json");
    json::bind_runtime_seed_source(pak_seed_source);
    const auto pak_root = root / "pak-only";
    std::filesystem::create_directory(pak_root);
    assert(!std::filesystem::exists(pak_root / "slot_data.json"));
    auto remote_potion = make_row(0, "Event:POTION", "bp_tres_get_item", true);
    remote_potion["from_player"] = 2; // ID collision must not turn this into a local reward bundle.
    auto pak_receipts = input;
    pak_receipts["items"] = Json::array({remote_potion});
    pak_receipts["server_checked_locations"] = Json::array();
    reconciliation::start(pak_root);
    reconciliation::observe("runtime_receipt_catalog.json", encode(pak_receipts));
    reconciliation::file_open(L"synthetic/KHIII_slot0.bin", GENERIC_READ, 100, ticks + 5);
    until([] { return reconciliation::view()->ready && reconciliation::view()->delivery != nullptr; });
    assert(reconciliation::view()->delivery->grants.size() == 1);
    assert(reconciliation::view()->delivery->grants.front().runtime_token == L"ITEM_POTION");
    reconciliation::observe("runtime_loot_event.json", encode(Json{{"type", "event"},
        {"bridge_key", "Event:FOREIGN"}, {"opened_at", "pak-foreign"}, {"actor_name", "foreign-test"}}));
    until([] { return reconciliation::view()->completed_checks.contains("Event:FOREIGN"); });
    const std::array<int64_t, 1> incoming_index{0};
    complete_receipts(incoming_index, reconciliation::view()->epoch);
    until([] { return reconciliation::view()->active.contains(0) && !reconciliation::view()->delivery; });
    reconciliation::observe("runtime_receipt_catalog.json", encode(pak_receipts));
    complete_receipts(incoming_index, reconciliation::view()->epoch);
    reconciliation::stop();
    assert(!reconciliation::view()->delivery);
    std::filesystem::remove_all(root);
    std::cout << "Native delivery worker restore, repair bundles, delayed receipts, notifications and stale ACK tests passed\n";
}
