// Runs the production transport without UE4SS or a game process.
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/core/types.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/checks/lucky_marks.h"
#include "kh3ap/bridge/checks/level_checks.h"
#include "kh3ap/bridge/session/deathlink.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/serialization/json.h"
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <iostream>
#include <thread>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 && argc != 3) return 2;
    const std::filesystem::path root{argv[1]};
    kh3ap::pipe::start(root, argc == 3 && std::wstring_view(argv[2]) == L"--tcp"
        ? kh3ap::pipe::Transport::LocalTcp : kh3ap::pipe::Transport::Auto);
    kh3ap::pipe::write(root / "runtime_context.json", R"({"context":"native-test"})");
    std::string previous;
    std::string previous_message;
    std::string previous_large;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (const auto command = kh3ap::pipe::read(root / "runtime_item_grant.json"); command && *command != previous)
        {
            previous = *command;
            kh3ap::pipe::write(root / "runtime_receipt_ack_event.json", *command);
            kh3ap::pipe::erase(root / "runtime_item_grant.json");
            for (int i = 0; i < 20; ++i)
            {
                kh3ap::pipe::write(root / "runtime_loot_event.json", "{\"check\":" + std::to_string(i) + "}");
                kh3ap::pipe::write(root / "current_state_write_polyhook_events.jsonl", "{\"level\":" + std::to_string(i + 2) + "}");
            }
        }
        if (kh3ap::pipe::read(root / "runtime_ap_message.json") == std::optional<std::string>{"{\"stop\":true}"}) break;
        if (const auto message = kh3ap::pipe::read(root / "runtime_ap_message.json"); message && *message != previous_message)
        {
            previous_message = *message;
            if (kh3ap::JsonFieldReader::int64_field(*message, "launch_wait_test").has_value())
            {
                std::stop_source cancellation;
                if (kh3ap::JsonFieldReader::bool_field(*message, "cancel").value_or(false))
                    cancellation.request_stop();
                kh3ap::pipe::write(root / "runtime_kh02_launch_request.json", R"({"request_id":"1:2:3"})");
                const auto timeout = kh3ap::JsonFieldReader::bool_field(*message, "timeout").value_or(false)
                    ? std::chrono::milliseconds(30) : std::chrono::milliseconds(3000);
                const auto reply = kh3ap::pipe::wait_and_take(root / "runtime_kh02_launch_response.json",
                                                             cancellation.get_token(), timeout);
                kh3ap::pipe::erase(root / "runtime_kh02_launch_request.json");
                kh3ap::pipe::write(root / "runtime_ap_message_result.json", reply ? R"({"received":true})" : R"({"received":false})");
            }
            else if (message->starts_with(R"({"notification_test":)"))
            {
                for (const auto& record : kh3ap::JsonFieldReader::object_array_field(*message, "records"))
                    kh3ap::pipe::write(root / "runtime_notification_event.json", record);
            }
            else if (message->starts_with(R"({"deathlink_test":)"))
            {
                const auto channel = kh3ap::JsonFieldReader::string_field(*message, "channel");
                const auto records = kh3ap::JsonFieldReader::object_array_field(*message, "records");
                if (channel && !records.empty())
                {
                    if (*channel == L"manual_player")
                    {
                        const auto key = kh3ap::json::Value::parse(records[0]).value("player_key", std::string{});
                        kh3ap::deathlink::observe_manual_player(
                            key,
                            kh3ap::JsonFieldReader::bool_field(records[0], "dead").value_or(false),
                            kh3ap::JsonFieldReader::bool_field(records[0], "ready").value_or(false));
                    }
                    else { kh3ap::pipe::write(root / *channel, records[0]); }
                }
                const auto view = kh3ap::deathlink::view();
                auto state = kh3ap::pipe::read(root / kh3ap::deathlink::state_file).value_or(R"({"pending":true})");
                state.pop_back();
                state += ",\"serial\":" + std::to_string(kh3ap::JsonFieldReader::int64_field(*message, "deathlink_test").value_or(0));
                state += ",\"fenced\":" + std::string(view->fenced ? "true" : "false");
                state += ",\"priority\":" + kh3ap::pipe::read(root / "runtime_item_grant.priority.json").value_or("null") + "}";
                kh3ap::pipe::write(root / "runtime_ap_message_result.json", state);
            }
            else if (message->starts_with(R"({"level_up_test":)"))
            {
                if (const auto level = kh3ap::JsonFieldReader::int64_field(*message, "level"))
                    kh3ap::level_checks::observe(
                        kh3ap::JsonFieldReader::bool_field(*message, "wrong_root").value_or(false) ? root / "other" : root,
                        static_cast<uint64_t>(*level));
                auto json = kh3ap::pipe::read(root / kh3ap::level_checks::state_file).value_or(R"({"pending":true})");
                json.pop_back();
                json += ",\"serial\":" + std::to_string(kh3ap::JsonFieldReader::int64_field(*message, "level_up_test").value_or(0)) + "}";
                kh3ap::pipe::write(root / "runtime_ap_message_result.json", json);
            }
            else if (message->starts_with(R"({"lucky_mark_test":)"))
            {
                const auto token = kh3ap::JsonFieldReader::string_field(*message, "token");
                if (token) kh3ap::lucky_marks::record(root, *token,
                    static_cast<int32_t>(kh3ap::JsonFieldReader::int64_field(*message, "result").value_or(712)),
                    static_cast<uint32_t>(kh3ap::JsonFieldReader::int64_field(*message, "number").value_or(0)));
                const auto state = kh3ap::lucky_marks::view();
                auto json = kh3ap::pipe::read(root / kh3ap::lucky_marks::state_file).value_or(R"({"pending":true})");
                json.pop_back();
                json += ",\"serial\":" + std::to_string(kh3ap::JsonFieldReader::int64_field(*message, "lucky_mark_test").value_or(0));
                json += ",\"milestones\":[";
                bool first = true;
                for (auto milestone : state->milestones) { if (!first) json += ','; first = false; json += std::to_string(milestone); }
                json += "]}";
                kh3ap::pipe::write(root / "runtime_ap_message_result.json", json);
            }
            else if (message->starts_with(R"({"reconciliation_test":)"))
            {
                const auto kind = kh3ap::JsonFieldReader::string_field(*message, "kind").value_or(L"");
                const auto slot = kh3ap::JsonFieldReader::int64_field(*message, "slot").value_or(0);
                const auto event = kh3ap::JsonFieldReader::int64_field(*message, "event").value_or(1);
                FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
                const uint64_t at = static_cast<uint64_t>(kh3ap::JsonFieldReader::int64_field(*message, "filetime")
                    .value_or(static_cast<int64_t>((uint64_t{ft.dwHighDateTime} << 32) | ft.dwLowDateTime)));
                if (kind == L"load" || kind == L"save_open")
                    kh3ap::reconciliation::file_open(L"kh3sv2/data/KHIII_slot" + std::to_wstring(slot) + L".bin",
                        kind == L"load" ? GENERIC_READ : GENERIC_WRITE, event, at,
                        kh3ap::JsonFieldReader::bool_field(*message, "open_success").value_or(true));
                else if (kind == L"auto_begin") kh3ap::reconciliation::auto_save_begin(at);
                else if (kind == L"save_closed") kh3ap::reconciliation::file_closed(event,
                    kh3ap::JsonFieldReader::bool_field(*message, "success").value_or(false));
                else if (kind == L"completion")
                {
                    const auto binding = kh3ap::reconciliation::view();
                    kh3ap::json::Value event_payload{{"owner", "dll"}, {"process_id", GetCurrentProcessId()},
                        {"seed_name", std::string(binding->seed.begin(), binding->seed.end())}, {"slot", binding->slot},
                        {"delivery_epoch", kh3ap::JsonFieldReader::int64_field(*message, "epoch").value_or(binding->epoch)},
                        {"request_id", "host-completion-" + std::to_string(kh3ap::JsonFieldReader::int64_field(*message, "reconciliation_test").value_or(0))},
                        {"status", "applied"}, {"applied_at", "2026-10-05T12:00:00Z"},
                        {"action_kind", "runtime_item_grant_batch"},
                        {"applied_indices", kh3ap::json::Value::array_t{kh3ap::JsonFieldReader::int64_field(*message, "index").value_or(-1)}}};
                    std::string encoded;
                    kh3ap::json::write(event_payload, encoded);
                    kh3ap::pipe::write(root / "runtime_item_apply_event.json", encoded);
                }
                else if (kind == L"context" || kind == L"death" || kind == L"new_game")
                {
                    const auto records = kh3ap::JsonFieldReader::object_array_field(*message, "records");
                    if (!records.empty()) kh3ap::reconciliation::observe(kind == L"context" ? "runtime_context.json" :
                        kind == L"death" ? "current_state_write_polyhook_events.jsonl" : "runtime_new_game_staging.json", records[0]);
                }
                else if (kind == L"progress")
                {
                    const auto channel = kh3ap::JsonFieldReader::string_field(*message, "channel").value_or(L"");
                    const auto records = kh3ap::JsonFieldReader::object_array_field(*message, "records");
                    if (!channel.empty() && !records.empty()) kh3ap::pipe::write(root / channel, records[0]);
                }
                else if (kind == L"inspect")
                {
                    auto state = kh3ap::pipe::read(root / "runtime_reconciliation_state.json").value_or(R"({"pending":true})");
                    state.pop_back(); state += ",\"serial\":" + std::to_string(event) +
                        ",\"saved_slots\":" + kh3ap::pipe::read(root / "runtime_save_receipts.json").value_or("{}") +
                        ",\"checks\":" + kh3ap::pipe::read(root / "runtime_check_state.json").value_or("{}") + "}";
                    kh3ap::pipe::write(root / "runtime_ap_message_result.json", state);
                }
                if (kind != L"inspect") kh3ap::pipe::write(root / "runtime_ap_message_result.json",
                    "{\"dispatched\":" + std::to_string(kh3ap::JsonFieldReader::int64_field(*message, "reconciliation_test").value_or(0)) + "}");
            }
            else if (message->starts_with(R"({"trap_submit":)"))
            {
                const auto [revision, request] = kh3ap::pipe::grant_batch(root / kh3ap::native_traps::request_file);
                if (request) kh3ap::native_traps::submit(request);
            }
            else if (message->starts_with(R"({"trap_apply":)"))
                kh3ap::native_traps::applied(kh3ap::JsonFieldReader::int64_field(*message, "trap_apply").value_or(-1));
            else if (message->starts_with(R"({"trap_inspect":)"))
            {
                const auto state = kh3ap::native_traps::view();
                kh3ap::pipe::write(root / "runtime_ap_message_result.json",
                    "{\"serial\":" + std::to_string(kh3ap::JsonFieldReader::int64_field(*message, "trap_inspect").value_or(0)) + ",\"pending\":" + std::to_string(state->pending) + ",\"accepted\":" + std::to_string(state->accepted) +
                    ",\"head\":" + std::to_string(state->head ? state->head->index : -1) +
                    ",\"ready\":" + (state->ready ? "true" : "false") + ",\"error\":\"" + state->error + "\"}");
            }
            else if (message->starts_with(R"({"inspect_prepared_grant":)"))
            {
                const auto [revision, batch] = kh3ap::pipe::grant_batch(root / "runtime_item_grant.priority.json");
                kh3ap::pipe::write(root / "runtime_ap_message_result.json",
                    "{\"revision\":" + std::to_string(revision) + ",\"count\":" +
                    std::to_string(batch ? batch->grants.size() : 0) + ",\"process_id\":" +
                    std::to_string(batch ? batch->process_id : 0) + "}");
            }
            else if (message->starts_with(R"({"inspect_grant_wake":)"))
            {
                kh3ap::pipe::take_updates(); // Other consumers cannot steal this wake.
                const bool requested = kh3ap::pipe::take_grant_updates();
                const bool repeated = kh3ap::pipe::take_grant_updates();
                kh3ap::pipe::write(root / "runtime_ap_message_result.json",
                    std::string{"{\"requested\":"} + (requested ? "true" : "false") +
                    ",\"repeated\":" + (repeated ? "true" : "false") +
                    ",\"generation\":" + std::to_string(kh3ap::pipe::input_generation()) + "}");
            }
            else if (message->starts_with(R"({"inspect_generation":)"))
            {
                const auto generation = kh3ap::pipe::input_generation();
                kh3ap::pipe::take_updates();
                kh3ap::pipe::write(root / "runtime_ap_message_result.json",
                    "{\"generation\":" + std::to_string(generation) +
                    ",\"after_take\":" + std::to_string(kh3ap::pipe::input_generation()) +
                    ",\"items_input_revision\":" + std::to_string(kh3ap::pipe::input_revision(root / "ap_items.json")) +
                    ",\"items_revision\":" + std::to_string(kh3ap::pipe::revision(root / "ap_items.json")) + "}");
            }
            else if (*message == R"({"inspect_recovery":true})")
            {
                const bool pending = kh3ap::pipe::read(root / "runtime_gameflow_command.json").has_value() ||
                    kh3ap::pipe::read(root / "runtime_warp_command.json").has_value();
                kh3ap::pipe::write(root / "runtime_ap_message_result.json",
                    pending ? R"({"pending":true})" : R"({"pending":false})");
            }
            else kh3ap::pipe::write(root / "runtime_ap_message_result.json", *message);
            std::cout << "control_received" << std::endl;
        }
        for (const auto& [command_name, result_name] : {
            std::pair{"runtime_gameflow_command.json", "runtime_gameflow_result.json"},
            std::pair{"runtime_warp_command.json", "runtime_warp_result.json"}})
        {
            if (const auto command = kh3ap::pipe::read(root / command_name))
            {
                kh3ap::pipe::write(root / result_name, *command);
                if (*command != R"({"hold":true})") kh3ap::pipe::erase(root / command_name);
            }
        }
        if (const auto large = kh3ap::pipe::read(root / "runtime_mom_clear_state.json"); large && *large != previous_large)
        {
            previous_large = *large;
            kh3ap::pipe::write(root / "runtime_context.json", *large);
            kh3ap::pipe::write(root / "runtime_map_ready.json", *large);
            kh3ap::pipe::write(root / "runtime_sora_state.json", *large);
            std::cout << "large_published" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    kh3ap::pipe::stop();
    return 0;
}
