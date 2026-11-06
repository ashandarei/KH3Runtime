#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include "kh3ap/features/world/portals/gate_opener_policy.h"
#include "kh3ap/bridge/rewards/reconciliation_projections.h"
#include "kh3ap/bridge/rewards/reconciliation_protocol.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <tuple>
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/features/player/characters/character_unlocks.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/features/kh02/rewards/kh02_reward_notice.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh3ap/features/kh02/progress/kh02_check_policy.h"
#include "kh3ap/bridge/rewards/receipt_accounting.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/bridge/catalog/catalog.h"
#include "kh3ap/bridge/session/runtime_status.h"
#include "kh3ap/bridge/rewards/receipts.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/bridge/checks/check_policy.h"
#include "kh3ap/bridge/catalog/chest_manifest.h"
#include "kh3ap/features/grants/delivery/runtime_loot_policy.h"
#include "kh3ap/bridge/serialization/json.h"
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace kh3ap::reconciliation
{
    using namespace protocol;

    namespace
    {
        constexpr auto vbonus_observation_capacity = 32;
        constexpr std::wstring_view report_item_prefix = L"REPORT_ITEM";
        constexpr auto report_number_digits = 2;
        constexpr auto report_item_token_chars = report_item_prefix.size() + report_number_digits;
        constexpr auto report_tens_index = report_item_prefix.size();
        constexpr auto report_units_index = report_tens_index + 1;
        constexpr auto item_progression_mask = 1;
        constexpr auto item_skip_balancing_mask = 8;
        constexpr auto item_priority_mask = item_progression_mask | item_skip_balancing_mask;
        constexpr auto ingredient_items_mode = 1;
        constexpr auto ingredient_spots_mode = 2;
        constexpr std::wstring_view save_slot_file_prefix = L"khiii_slot";
        constexpr std::wstring_view save_slot_file_suffix = L".bin";

        constexpr auto secret_report_count = 13;
        constexpr auto maximum_reconciliation_journal_bytes = 16 * 1024 * 1024;

        constexpr auto reconciliation_job_capacity = 16384;
        constexpr auto max_regrant_observations = 16384;
        constexpr auto max_catalog_item_index = 1000000;
        constexpr auto max_journal_save_slot = 99;
        constexpr auto max_reconciliation_request_id_bytes = 160;
        constexpr auto catalog_event_capacity = 2048;
        constexpr auto max_save_transactions = 128;
        constexpr auto item_apply_batch_capacity = 128;

        using Json = kh3ap::json::Value;
        constexpr size_t notification_queue_capacity = 4096;
        struct Job
        {
            std::string kind, data;
            int64_t slot{-1}, epoch{};
            uint64_t event{}, time{};
            bool success{};
            std::optional<kh02_progress::Update> kh02_update;
            std::optional<kh02_progress::Binding> kh02_reset;

            std::array<uint8_t, vbonus_observation_capacity> bonuses{};
        };
        struct Save
        {
            int64_t slot{};
            Receipts snapshot;
            std::string at;
            bool correlated{};
            kh02_progress::SnapshotPtr kh02_progress;
        };
        struct Catalog
        {
            bool replay{};
            bool passthrough{};
        };
        std::atomic<std::shared_ptr<const View>> snapshot{std::make_shared<const View>()};
        std::atomic<int64_t> current_epoch{1};
        std::atomic<bool> overflow{};
        std::atomic<bool> inventory_observer{};
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<Job> jobs;
        bool stopping{}, running{};
        std::thread worker;
        std::filesystem::path root;

        auto enqueue(Job job) -> void
        {
            std::lock_guard lock{mutex};
            if (!running || stopping)
            {
                return;
            }
            if (jobs.size() >= reconciliation_job_capacity)
            {
                overflow.store(true);
                changed.notify_one();
                return;
            }
            jobs.push_back(std::move(job));
            changed.notify_one();
        }

        class State
        {
            std::string seed, error, hint_error;
            int64_t ap_slot{};
            bool initialized{}, loaded{}, dead{};
            int64_t generation{}, processed_epoch{1};
            std::map<int64_t, Receipts> slots;
            kh02_progress::Bank kh02;
            std::map<int64_t, std::string> kh02_invalid_slots;
            std::string kh02_error;
            std::map<int64_t, Catalog> catalog;
            std::map<int64_t, item_delivery::ReceiptTemplate> delivery_catalog;
            native_catalog::Seed seed_catalog;
            std::shared_ptr<const item_delivery::ExpandedCatalog> expanded;
            std::set<int64_t> forced, submitted_traps;
            std::set<std::string> repairs;
            runtime_loot::Policy loot_policy;
            checks::Policy check_policy;
            std::vector<kh02_checks::Condition> kh02_check_conditions;
            std::set<std::wstring> kh02_ap_chests;
            std::set<int64_t> server_checked;
            std::map<std::string, int64_t> location_ids;
            std::map<std::string, std::string> extra_triggers;
            struct Conditional
            {
                std::string key, trigger;
                int64_t unless_item{};
                std::vector<int64_t> required_items;
            };
            std::vector<Conditional> conditional_checks;
            std::set<int64_t> received_item_ids;
            int64_t ingredient_mode{}, classic_mode{}, coliseum_mode{};
            bool adversary_checks{};
            chest_manifest::Catalog chest_catalog;
            std::string keyblade_manifest, music_manifest;
            std::shared_ptr<const chest_manifest::Snapshot> chest_snapshot;
            std::set<int64_t> chest_checked;
            bool chest_manifest_dirty{true};
            std::map<std::string, runtime_loot::Location> locations;
            std::map<std::pair<std::string, std::string>, std::string> progress_keys;
            std::map<std::string, int64_t> repair_generation;
            std::map<std::string, Job> reobserved;
            std::map<std::string, int64_t> already_notified;
            std::deque<RuntimeApMessageRequest> messages;
            uint64_t notification_sequence{};
            bool catalog_loaded{};
            uint64_t delivery_revision{};
            std::string delivery_signature;
            std::map<uint64_t, Save> saves;
            Receipts active, disk_checkpoint, retry_checkpoint;
            bool retry_valid{};
            bool inventory_tracking{inventory_observer.load()}, inventory_valid{},
                inventory_restored{};
            Receipts inventory_backup;
            std::set<int64_t> inventory_partial, retry_preserved;
            std::string retry_map, retry_pawn, pawn, death_map, recovery_source, recovery_at,
                death_key;
            int64_t loaded_slot{};
            std::set<int64_t> pending;
            std::optional<Save> pending_autosave;
            Json current, retry, baseline, last_load;
            Json context;
            Json deathlink;
            std::map<int64_t, Json> receipt_facts;
            std::string description_payload;
            std::string description_items;
            Json::object_t receipt_descriptions;
            receipt_accounting::Accounting accounting;
            uint64_t accounting_revision{};
            receipt_accounting::Accounting committed_accounting;
            std::vector<Json> receipt_acks;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            std::chrono::steady_clock::time_point receipt_started;
#endif
            ReconciliationProjections projections;
            uint64_t receipt_catalog_revision{};
            uint64_t description_revision{};
            runtime_status::Snapshot runtime_status;
            std::set<int64_t> hint_reports;
            gate_opener::State gates;
            auto gate_receipts() const -> std::set<int64_t>
            {
                std::set<int64_t> result;
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (active.contains(index) && item.primary.grant.runtime_token == L"KEY_ITEM20")
                    {
                        result.insert(index);
                    }
                }
                return result;
            }
            std::map<int64_t, int64_t> report_receipts;
            Json hint_targets = Json::object_t{};
            std::deque<Job> before_catalog;

            auto bound(const Json& j) const -> bool
            {
                return number(field(j, "process_id")) == GetCurrentProcessId() &&
                       text(field(j, "seed_name")) == seed && number(field(j, "slot")) == ap_slot;
            }
            auto add_replay_preview(Json& value, const Receipts& saved) const -> void
            {
                const bool ready = catalog_loaded && error.empty();
                std::set<int64_t> eligible;
                if (ready)
                {
                    for (const auto& [index, item] : catalog)
                    {
                        if (item.replay)
                        {
                            eligible.insert(index);
                        }
                    }
                }
                value["replay_preview_ready"] = ready;
                value["pending_replay_items"] =
                    client_presentation::replay_items(saved, eligible, receipt_descriptions);
            }
            auto checkpoint(const Receipts& r, int64_t slot, std::string source,
                            std::string at) const -> Json
            {
                Json label = slot == 0 ? Json("AUTO") : Json(slot);
                Json value{{"protocol_name", "kh3_ap_file_bridge"},
                           {"protocol_version", 1},
                           {"schema", "kh3ap.save_slot.v1"},
                           {"process_id", GetCurrentProcessId()},
                           {"seed_name", seed},
                           {"slot", ap_slot},
                           {"error", error},
                           {"owner", "dll"},
                           {"event", "runtime_current_save_slot"},
                           {"status", "known"},
                           {"source", source},
                           {"load_kind", source},
                           {"save_slot", label},
                           {"save_slot_label", label},
                           {"source_slot", label},
                           {"slot_file", slot_path(slot)},
                           {"observed_at_utc", at},
                           {"saved_receipts", receipt_json(r)},
                           {"replay_cutoff_index", r.through},
                           {"last_applied_index_at_save", r.through},
                           {"ap_index_at_save", r.through}};
                add_replay_preview(value, r);
                return value;
            }
            auto update_pending() -> void
            {
                pending.clear();
                if (!loaded)
                {
                    return;
                }
                for (const auto& [index, item] : catalog)
                {
                    if (item.replay && !active.contains(index))
                    {
                        pending.insert(index);
                    }
                }
            }
            auto update_window() -> void
            {
                if (retry.is_null())
                {
                    return;
                }
                Json::array_t indices;
                for (auto i : pending)
                {
                    indices.emplace_back(i);
                }
                retry["pending_replay_indices"] = std::move(indices);
                retry["pending_replay_index_count"] = pending.size();
                retry["status"] = pending.empty() ? "consumed" : "pending";
                if (pending.empty())
                {
                    retry["consumed_reason"] = "native_receipt_completion";
                    retry["consumed_at_utc"] = utc();
                }
            }
            auto rollback(Receipts r, std::string source, std::string at) -> void
            {
                active = std::move(r);
                messages.clear();
                reobserved.clear();
                // One-shot/non-runtime AP bookkeeping remains outside restore
                // membership; it must never resurrect a completed trap.
                for (const auto& [index, item] : catalog)
                {
                    if (item.passthrough)
                    {
                        active.add(index);
                    }
                }
                recovery_source = std::move(source);
                recovery_at = std::move(at);
                ++generation;
                update_pending();
                retry = checkpoint(active, loaded_slot, recovery_source, recovery_at);
                retry["event"] = "runtime_death_retry_replay_window";
                retry["cutoff_index"] = active.through;
                retry["continue_trigger_key"] =
                    std::to_string(GetCurrentProcessId()) + ":" + std::to_string(generation);
                retry["runtime_context"] = context;
                update_window();
            }
            auto journal() const -> Json
            {
                Json::array_t records, items, repair_keys, observed, completed, forced_indices;
                for (const auto& [slot, r] : slots)
                {
                    records.emplace_back(
                        Json{{"save_slot", slot},
                             {"saved_receipts", receipt_json(r)},
                             {"kh02_progress",
                              kh02_progress::to_json(kh02.slots.contains(slot) ? kh02.slots.at(slot)
                                                                               : nullptr)}});
                }
                for (const auto& [index, item] : catalog)
                {
                    items.emplace_back(Json{{"index", index},
                                            {"replay", item.replay},
                                            {"passthrough", item.passthrough}});
                }
                for (const auto& key : repairs)
                {
                    repair_keys.emplace_back(key);
                }
                for (const auto& key : check_policy.observed)
                {
                    observed.emplace_back(key);
                }
                for (const auto& key : check_policy.completed)
                {
                    completed.emplace_back(key);
                }
                for (const auto index : forced)
                {
                    forced_indices.emplace_back(index);
                }
                Json::array_t reports;
                for (const auto report : hint_reports)
                {
                    reports.emplace_back(report);
                }
                Json::array_t gate_choices;
                for (const auto& [receipt, portal] : gates.spent)
                {
                    gate_choices.emplace_back(Json{{"receipt", receipt}, {"portal", portal}});
                }
                return Json{{"schema", "kh3ap.native_reconciliation.v4"},
                            {"seed_name", seed},
                            {"slot", ap_slot},
                            {"receipt_accounting", accounting.checkpoint()},
                            {"hint_report_numbers", std::move(reports)},
                            {"gate_openers", std::move(gate_choices)},
                            {"slots", std::move(records)},
                            {"catalog", std::move(items)},
                            {"repair_keys", std::move(repair_keys)},
                            {"observed_checks", std::move(observed)},
                            {"completed_checks", std::move(completed)},
                            {"goal_complete", check_policy.goal},
                            {"forced_indices", std::move(forced_indices)},
                            {"deathlink", deathlink},
                            {"complete", true}};
            }
            auto reconcile_local_receipts() -> void
            {
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (!item.local || item.level_up || item.always_on || item.bridge_required)
                    {
                        continue;
                    }
                    const auto location = location_ids.find(item.bridge_key);
                    if (!check_policy.completed.contains(item.bridge_key) &&
                        location != location_ids.end() && server_checked.contains(location->second))
                    {
                        forced.insert(index);
                    }
                    auto& receipt = catalog.at(index);
                    if (forced.contains(index))
                    {
                        receipt = {item.supported, false};
                    }
                    else if (!repairs.contains(item.bridge_key) &&
                             check_policy.completed.contains(item.bridge_key))
                    {
                        receipt = {false, true};
                        if (loaded)
                        {
                            active.add(index);
                        }
                    }
                }
                update_pending();
            }
            auto resolve_checks(bool announce) -> void
            {
                auto added = check_policy.resolve();
                for (const auto& [extra, trigger] : extra_triggers)
                {
                    if (check_policy.completed.contains(trigger) &&
                        check_policy.completed.insert(extra).second)
                    {
                        added.insert(extra);
                    }
                }
                if (catalog_loaded)
                {
                    for (const auto& rule : conditional_checks)
                    {
                        if (!received_item_ids.contains(rule.unless_item) &&
                            check_policy.completed.contains(rule.trigger) &&
                            std::all_of(rule.required_items.begin(), rule.required_items.end(),
                                        [this](int64_t item) { return received_item_ids.contains(item); }) &&
                            check_policy.completed.insert(rule.key).second)
                        {
                            added.insert(rule.key);
                        }
                    }
                }
                for (const auto& key : added)
                {
                    const bool fresh = loot_policy.complete(key, processed_epoch);
                    if (announce && fresh)
                    {
                        const auto message = runtime_loot::foreign_message(locations.at(key));
                        if (!message.empty())
                        {
                            notify(key, "applied", message, true);
                        }
                    }
                }
                reconcile_local_receipts();
            }
            auto notify(const std::string& key, std::string status, std::string message,
                        bool in_game) -> void
            {
                const auto& location = locations.at(key);
                const auto id = "native-notification-" + std::to_string(GetCurrentProcessId()) +
                                "-" + std::to_string(GetTickCount64()) + "-" +
                                std::to_string(++notification_sequence);
                if (in_game)
                {
                    if (messages.size() >= notification_queue_capacity)
                    {
                        throw std::runtime_error("notification_queue_capacity");
                    }
                    messages.push_back(
                        RuntimeApMessageRequest{.request_id = utf8_to_wstring(id),
                                                .message = utf8_to_wstring(message),
                                                .bridge_key = utf8_to_wstring(key),
                                                .location_name = utf8_to_wstring(location.name),
                                                .item_name = utf8_to_wstring(location.item)});
                }
                pipe::write(root / channels::runtime_notification_event,
                            encode(Json{{"schema", "kh3ap.notification.v1"},
                                        {"owner", "dll"},
                                        {"process_id", GetCurrentProcessId()},
                                        {"seed_name", seed},
                                        {"slot", ap_slot},
                                        {"delivery_epoch", processed_epoch},
                                        {"request_id", id},
                                        {"bridge_key", key},
                                        {"location_name", location.name},
                                        {"item_name", location.item},
                                        {"status", status},
                                        {"message", message},
                                        {"event", "runtime_loot"}}));
            }
            auto repeat(const std::string& key, const Job& job) -> void
            {
                bool known{}, queued{}, reopened{};
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (!item.local || item.bridge_key != key)
                    {
                        continue;
                    }
                    known = true;
                    if (!item_delivery::repairable(item))
                    {
                        continue;
                    }
                    // A repeated hook during an unfinished repair only refreshes
                    // observation. It cannot invalidate partial bundle successes.
                    if (repairs.contains(key) && !active.contains(index))
                    {
                        queued = true;
                        continue;
                    }
                    repairs.insert(key);
                    active.remove(index);
                    catalog.at(index) = Catalog{true, false};
                    queued = true;
                    reopened = true;
                }
                if (queued)
                {
                    if (reopened)
                    {
                        ++repair_generation[key];
                        delivery_signature.clear();
                        update_pending();
                        commit();
                        notify(key, "regrant_queued",
                               "Regrant queued after rollback: " + locations.at(key).name, false);
                    }
                }
                else if (!known && !locations.at(key).foreign)
                {
                    // The AP receipt may arrive after the event. Preserve the
                    // observation until its declarative reward template arrives.
                    reobserved[key] = job;
                    if (reobserved.size() > max_regrant_observations)
                    {
                        throw std::runtime_error("regrant_observation_capacity");
                    }
                }
                else if (!locations.at(key).always_on)
                {
                    const auto prior = already_notified.find(key);
                    if (prior == already_notified.end() || job.epoch != prior->second)
                    {
                        already_notified[key] = job.epoch;
                        const auto& location = locations.at(key);
                        notify(key, "already_checked",
                               "AP: Already obtained " +
                                   (location.item.empty() ? location.name : location.item),
                               true);
                    }
                }
            }
            auto loot(const Job& job) -> void
            {
                if (!loaded || dead || job.epoch != processed_epoch)
                {
                    return;
                }
                const auto event = decode(job.data);
                const auto key = text(field(event, "bridge_key"));
                const auto found = locations.find(key);
                if (found == locations.end())
                {
                    return;
                }
                const auto decision =
                    loot_policy.observe({.key = key,
                                         .type = text(field(event, "type")),
                                         .source = text(field(event, "source")),
                                         .resolution = text(field(event, "resolution_kind")),
                                         .mapping = text(field(event, "mapping_reason")),
                                         .epoch = job.epoch},
                                        found->second);
                if (decision == runtime_loot::Decision::fresh)
                {
                    // Native progress owns treasure, level and Lucky Mark checks.
                    // Event hooks still pass through the encounter prerequisites.
                    loot_policy.checked.erase(key);
                    const auto& row = check_policy.locations.at(key);
                    check_policy.observe(row.table, row.id);
                    resolve_checks(true);
                }
                else if (decision == runtime_loot::Decision::repeated &&
                         check_policy.completed.contains(key))
                {
                    repeat(key, job);
                }
            }
            auto progress(const Json& input, std::string_view type, bool announce) -> void
            {
                if (text(field(input, "seed_name")) != seed ||
                    number(field(input, "slot")) != ap_slot)
                {
                    return;
                }
                const auto kind = type == "treasure" ? "chest" : std::string(type);
                if (text(field(input, "schema")) != "kh3ap." + kind + "_progress.v1")
                {
                    return;
                }
                // Lucky Mark is the native channel/schema name; generated AP
                // location rows use the lucky_emblem type.
                const auto location_type =
                    type == "lucky_mark" ? "lucky_emblem" : std::string(type);
                for (const auto& id : array(field(input, "completed")))
                {
                    const auto found = progress_keys.find({location_type, text(id)});
                    if (found == progress_keys.end())
                    {
                        continue;
                    }
                    const auto& key = found->second;
                    const auto& row = check_policy.locations.at(key);
                    check_policy.observe(row.table, row.id);
                }
                resolve_checks(announce);
            }
            auto table_progress(const Json& input, std::string_view table, std::string_view member)
                -> void
            {
                if (text(field(input, "seed_name")) != seed ||
                    number(field(input, "slot")) != ap_slot)
                {
                    return;
                }
                for (const auto& id : array(field(input, member)))
                {
                    check_policy.observe(table, text(id));
                }
                resolve_checks(true);
            }
            auto commit() -> void
            {
                std::set<int64_t> accounted;
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (active.contains(index))
                    {
                        accounted.insert(index);
                    }
                }
                if (accounting.remember(accounted, utc()))
                {
                    ++accounting_revision;
                }
                const auto data = encode(journal());
                if (data.size() > maximum_reconciliation_journal_bytes)
                {
                    throw std::runtime_error("journal_size_limit");
                }
                const auto target = root / journal_file;
                const auto temporary = std::filesystem::path(target.wstring() + L".tmp");
                HANDLE f = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
                if (f == INVALID_HANDLE_VALUE)
                {
                    throw std::runtime_error("journal_open_failed");
                }
                DWORD written{};
                const bool ok =
                    WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                    written == data.size() && FlushFileBuffers(f);
                CloseHandle(f);
                if (!ok || !MoveFileExW(temporary.c_str(), target.c_str(),
                                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    throw std::runtime_error("journal_commit_failed");
                }
                committed_accounting = accounting;
            }
            auto publish_receipt_projections(const Json& summary, const Json& repairs_json,
                                             const std::vector<int64_t>& pending_indices) -> void
            {
                std::set<int64_t> active_indices;
                for (const auto& i :
                     summary["applied_item_indices"].get_ref<const Json::array_t&>())
                {
                    active_indices.insert(number(i));
                }
                std::map<int64_t, std::string> policies;
                for (const auto& [index, item] : delivery_catalog)
                {
                    const bool trap =
                        item.primary.grant.grant_method == L"direct_player_bad_status";
                    policies[index] = trap                       ? "one_time"
                                      : !item.supported          ? "none"
                                      : catalog.at(index).replay ? "save_load_replay"
                                                                 : "local_seed_reward";
                }
                Json identity{{"owner", "dll"},
                              {"process_id", GetCurrentProcessId()},
                              {"seed_name", seed},
                              {"slot", ap_slot},
                              {"delivery_epoch", processed_epoch}};
                const bool ready = initialized && loaded && !dead && error.empty() &&
                                   processed_epoch == current_epoch.load();
                const std::string reason = error.empty() ? !initialized ? "awaiting_receipt_catalog"
                                                           : !loaded    ? "awaiting_load"
                                                           : dead       ? "game_over"
                                                                        : "ready"
                                                         : error;
                projections.publish_receipts(
                    root, accounting, identity, receipt_descriptions, receipt_facts,
                    {accounting_revision, receipt_catalog_revision, policies, active_indices,
                     pending_indices, summary, repairs_json, forced, ready, reason,
                     processed_epoch});
            }
            auto publish_runtime_status() const -> void
            {
                auto payload =
                    runtime_status.project(GetCurrentProcessId(), seed, ap_slot, processed_epoch,
                                           initialized && loaded && !dead && error.empty() &&
                                               processed_epoch == current_epoch.load());
                payload["error"] = error;
                pipe::write(root / channels::runtime_status, encode(payload));
            }
            auto publish_chests() -> void
            {
                if (!keyblade_manifest.empty())
                {
                    if (!error.empty())
                    {
                        chest_snapshot.reset();
                        pipe::erase(root / channels::runtime_music_chests);
                        pipe::erase(root / channels::runtime_keyblade_chests);
                    }
                    else
                    {
                        if (chest_manifest_dirty || chest_checked != server_checked ||
                            !chest_snapshot)
                        {
                            auto next_music =
                                encode(chest_catalog.music(server_checked, GetCurrentProcessId()));
                            if (!chest_snapshot || next_music != music_manifest)
                            {
                                chest_snapshot = std::make_shared<const chest_manifest::Snapshot>(
                                    chest_catalog.snapshot(server_checked, GetCurrentProcessId(),
                                                           root, utf8_to_wstring));
                            }
                            music_manifest = std::move(next_music);
                            chest_checked = server_checked;
                            chest_manifest_dirty = false;
                        }
                        pipe::write(root / channels::runtime_music_chests, music_manifest);
                        pipe::write(root / channels::runtime_keyblade_chests, keyblade_manifest);
                    }
                }
            }
            auto publish() -> void
            {
                publish_chests();
                if (error.empty() && loaded)
                {
                    auto next_reports = hint_reports;
                    for (const auto& [index, report] : report_receipts)
                    {
                        if (active.contains(index))
                        {
                            next_reports.insert(report);
                        }
                    }
                    if (next_reports != hint_reports)
                    {
                        const auto previous = hint_reports;
                        hint_reports = std::move(next_reports);
                        try
                        {
                            commit();
                        }
                        catch (...)
                        {
                            hint_reports = previous;
                            throw;
                        }
                    }
                }
                update_window();
                auto next = std::make_shared<View>();
                next->ready = initialized && loaded && !dead && error.empty() &&
                              processed_epoch == current_epoch.load();
                next->epoch = current_epoch.load();
                next->error = error;
                next->active = active;
                next->seed = utf8_to_wstring(seed);
                next->slot = ap_slot;
                next->gates = gates;
                next->gate_receipts = gate_receipts();
                next->kh02_progress_ready = initialized && loaded && error.empty() &&
                                            kh02_error.empty() &&
                                            processed_epoch == current_epoch.load();
                next->kh02_progress_revision = kh02.revision;
                next->loaded_save_slot = loaded_slot;
                next->kh02_progress = kh02.active;
                next->kh02_progress_error = kh02_error;
                next->kh02_ap_chests = kh02_ap_chests;
                next->chests = chest_snapshot;
                next->completed_checks = check_policy.completed;
                next->goal_complete = check_policy.goal;
                next->adversary_checks = adversary_checks;
                if (next->ready && !messages.empty())
                {
                    next->message =
                        std::make_shared<const RuntimeApMessageRequest>(messages.front());
                }
                next->deathlink_request = text(field(deathlink, "request_id"));
                next->expanded = expanded;
                Json::array_t delivery_indices;
                auto batch = std::make_shared<RuntimeItemGrantBatchRequest>();
                batch->request_schema = L"kh3ap.item_delivery.v1";
                batch->process_id = GetCurrentProcessId();
                batch->delivery_epoch = next->epoch;
                batch->seed_name = utf8_to_wstring(seed);
                batch->slot = ap_slot;
                batch->action_kind = L"runtime_item_grant_batch";
                batch->reapply_context_key = L"native_delivery:" +
                                             std::to_wstring(batch->process_id) + L":" +
                                             std::to_wstring(next->epoch);
                auto traps = std::make_shared<RuntimeItemGrantBatchRequest>(*batch);
                traps->action_kind = L"deferred_bad_status_trap";
                std::string signature = std::to_string(next->epoch) + ":";
                // Presentation includes pending receipts while dead/blocked too;
                // preserve that broader gate and share the ready-case schedule.
                auto plan =
                    catalog_loaded && loaded && expanded
                        ? item_delivery::schedule_catalog(delivery_catalog, *expanded, active,
                                                          forced, repairs, retry_preserved)
                        : item_delivery::ScheduledDelivery{};
                if (catalog_loaded && next->ready)
                {
                    for (auto index : plan.pending)
                    {
                        delivery_indices.emplace_back(index);
                    }
                    for (auto& grant : plan.grants)
                    {
                        const auto& item = delivery_catalog.at(grant.index);
                        signature += std::to_string(grant.index) + ",";
                        grant.action_kind = batch->action_kind;
                        grant.reapply_context_key = batch->reapply_context_key;
                        if (repairs.contains(item.bridge_key))
                        {
                            grant.reapply_context_key +=
                                L":repair:" + std::to_wstring(repair_generation[item.bridge_key]);
                        }
                        grant.show_info = !item.local || item.level_up || item.bridge_required ||
                                          forced.contains(grant.index);
                        batch->grants.push_back(std::move(grant));
                    }
                    for (const auto& trap : plan.traps)
                    {
                        if (!submitted_traps.contains(trap.index))
                        {
                            traps->grants.push_back(trap);
                        }
                    }
                }
                // Only pending indices are needed by the presentation pass.
                plan.grants.clear();
                plan.traps.clear();
                if (signature != delivery_signature)
                {
                    delivery_signature = signature;
                    ++delivery_revision;
                }
                next->delivery_revision = delivery_revision;
                batch->request_id = L"native-delivery-" + std::to_wstring(GetCurrentProcessId()) +
                                    L"-" + std::to_wstring(delivery_revision);
                for (auto& grant : batch->grants)
                {
                    grant.request_id = batch->request_id;
                }
                if (!batch->grants.empty())
                {
                    next->delivery = batch;
                }
                snapshot.store(next);
                Json::array_t indices, pending_repairs;
                for (auto index : pending)
                {
                    indices.emplace_back(index);
                }
                Json::array_t applied_indices;
                int64_t last_applied = -1;
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (active.contains(index))
                    {
                        applied_indices.emplace_back(index);
                        if (index == last_applied + 1)
                        {
                            last_applied = index;
                        }
                    }
                }
                Json summary{{"received_count", delivery_catalog.size()},
                             {"applied_count", applied_indices.size()},
                             {"pending_count", delivery_indices.size()},
                             {"last_applied_index", last_applied},
                             {"highest_received_index",
                              delivery_catalog.empty() ? -1 : delivery_catalog.rbegin()->first},
                             {"applied_item_indices", std::move(applied_indices)}};
                std::set<std::string> repair_pending;
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (repairs.contains(item.bridge_key) && !active.contains(index))
                    {
                        repair_pending.insert(item.bridge_key);
                    }
                }
                for (const auto& key : repair_pending)
                {
                    pending_repairs.emplace_back(key);
                }
                Json state{{"schema", "kh3ap.native_reconciliation_state.v1"},
                           {"owner", "dll"},
                           {"process_id", GetCurrentProcessId()},
                           {"seed_name", seed},
                           {"slot", ap_slot},
                           {"deathlink_request_id", next->deathlink_request},
                           {"ready", next->ready},
                           {"initialized", initialized},
                           {"reason", error.empty() ? !initialized ? "awaiting_receipt_catalog"
                                                      : !loaded    ? "awaiting_load"
                                                      : dead       ? "game_over"
                                                                   : "ready"
                                                    : error},
                           {"delivery_epoch", next->epoch},
                           {"generation", generation},
                           {"active_receipts", receipt_json(active)},
                           {"description_revision", description_revision},
                           {"delivery_summary", std::move(summary)},
                           {"pending_replay_indices", std::move(indices)},
                           {"recovery_source", recovery_source},
                           {"pending_delivery_indices", std::move(delivery_indices)},
                           {"delivery_owner", "dll"},
                           {"pending_regrant_bridge_keys", std::move(pending_repairs)},
                           {"server_forced_local_item_indices",
                            Json::array_t(forced.begin(), forced.end())},
                           {"notification_owner", "dll"},
                           {"recovery_observed_at_utc", recovery_at},
                           {"runtime_context", context},
                           {"last_save_load_event", last_load}};
                publish_receipt_projections(state["delivery_summary"],
                                            state["pending_regrant_bridge_keys"], plan.pending);
                pipe::write(root / channels::runtime_reconciliation_state, encode(state));
                publish_runtime_status();
                Json::object_t reports;
                Json::array_t missing;
                if (error.empty() && hint_error.empty())
                {
                    for (const auto report : hint_reports)
                    {
                        const auto key = std::to_string(report);
                        if (hint_targets.contains(key))
                        {
                            reports.emplace(key, hint_targets[key]);
                        }
                        else
                        {
                            missing.emplace_back(report);
                        }
                    }
                }
                pipe::write(root / channels::runtime_secret_report_hints,
                            encode(Json{{"schema", "kh3ap.secret_report_hints.v1"},
                                        {"owner", "dll"},
                                        {"process_id", GetCurrentProcessId()},
                                        {"seed_name", seed},
                                        {"slot", ap_slot},
                                        {"reports", std::move(reports)},
                                        {"missing_report_numbers", std::move(missing)},
                                        {"error", error.empty() ? hint_error : error}}));
                if (catalog_loaded)
                {
                    pipe::write(root / channels::runtime_item_descriptions, description_payload);
                }
                Json::array_t checked;
                for (const auto& key : check_policy.completed)
                {
                    checked.emplace_back(key);
                }
                pipe::write(root / channels::runtime_check_state,
                            encode(Json{{"schema", "kh3ap.check_state.v1"},
                                        {"owner", "dll"},
                                        {"process_id", GetCurrentProcessId()},
                                        {"seed_name", seed},
                                        {"slot", ap_slot},
                                        {"ready", error.empty()},
                                        {"reason", error},
                                        {"checked_locations", std::move(checked)},
                                        {"goal_complete", check_policy.goal},
                                        {"delivery_epoch", next->epoch}}));
                if (!current.is_null())
                {
                    current["error"] = error;
                    add_replay_preview(current, receipts(current["saved_receipts"]));
                    pipe::write(root / channels::runtime_current_save_slot, encode(current));
                }
                if (!retry.is_null())
                {
                    pipe::write(root / channels::runtime_death_retry_replay_window, encode(retry));
                }
                if (!baseline.is_null())
                {
                    pipe::write(root / channels::runtime_death_retry_baseline, encode(baseline));
                }
                std::set<int64_t> replay_eligible;
                for (const auto& [index, item] : catalog)
                {
                    if (item.replay)
                    {
                        replay_eligible.insert(index);
                    }
                }
                projections.publish_saves(
                    root, {slots, replay_eligible, description_revision, catalog_loaded, error},
                    [&]()
                    {
                        Json ledger{{"protocol_name", "kh3_ap_file_bridge"},
                                    {"protocol_version", 1},
                                    {"owner", "dll"},
                                    {"schema", "kh3ap.save_receipts.v1"},
                                    {"process_id", GetCurrentProcessId()},
                                    {"seed_name", seed},
                                    {"slot", ap_slot},
                                    {"error", error},
                                    {"slots", Json::object_t{}}};
                        for (const auto& [slot, r] : slots)
                        {
                            ledger["slots"][slot_path(slot)] =
                                checkpoint(r, slot, "native_save_checkpoint", "");
                        }
                        return ledger;
                    });
                if (initialized)
                {
                    pipe::notify_grants();
                }
                if (!traps->grants.empty())
                {
                    traps->request_id = L"native-delivery-traps-" +
                                        std::to_wstring(GetCurrentProcessId()) + L"-" +
                                        std::to_wstring(++delivery_revision);
                    for (auto& grant : traps->grants)
                    {
                        grant.request_id = traps->request_id;
                        grant.action_kind = traps->action_kind;
                        submitted_traps.insert(grant.index);
                    }
                    native_traps::submit(traps);
                }
            }
            auto metadata(const Json& input) -> void
            {
                if (!bound(input) || text(field(input, "schema")) != "kh3ap.receipt_catalog.v4")
                {
                    return;
                }
                const auto previous_slots = slots;
                const auto previous_catalog = catalog;
                const auto previous_active = active;
                const auto previous_checked = server_checked;
                const auto previous_item_ids = received_item_ids;
                decltype(delivery_catalog) previous_delivery;
                decltype(receipt_facts) previous_facts;
                const auto previous_expanded = expanded;
                const bool previous_loaded = catalog_loaded;
                bool replaced{};
                try
                {
                    std::map<int64_t, Catalog> next;
                    server_checked.clear();
                    for (const auto& i : array(field(input, "server_checked_locations")))
                    {
                        server_checked.insert(number(i));
                    }
                    auto next_delivery = delivery_catalog;
                    auto next_facts = receipt_facts;
                    for (const auto& row : array(field(input, "items")))
                    {
                        const auto index = number(field(row, "index"));
                        if (index < 0 || index > max_catalog_item_index || next.contains(index))
                        {
                            throw std::runtime_error("invalid_receipt_catalog");
                        }
                        received_item_ids.insert(number(field(row, "item_id"), 0));
                        auto item = seed_catalog.decode(row);
                        const auto& key = item.bridge_key;
                        const bool repairing = repairs.contains(key);
                        const bool one_shot = item.supported && item.primary.grant.grant_method ==
                                                                    L"direct_player_bad_status";
                        const bool replay =
                            repairing || (!one_shot && item.supported &&
                                          (!item.local || item.level_up || item.bridge_required ||
                                           forced.contains(index)));
                        const auto prior = catalog.find(index);
                        const bool pass =
                            !replay && prior != catalog.end() && prior->second.passthrough;
                        next.emplace(index, Catalog{replay, pass});
                        next_delivery[index] = std::move(item);
                        next_facts[index] = row;
                        if (pass && loaded)
                        {
                            active.add(index);
                        }
                    }
                    // Network reconnection may replay an older catalog. It cannot
                    // erase native-known receipts or change their replay identity.
                    for (const auto& [index, item] : next)
                    {
                        const auto old = catalog.find(index);
                        const bool promoted =
                            old != catalog.end() && !old->second.replay && item.replay;
                        if (promoted)
                        {
                            active.remove(index);
                        }
                        catalog[index] = Catalog{
                            item.replay, !item.replay && (item.passthrough ||
                                                          (!promoted && old != catalog.end() &&
                                                           old->second.passthrough))};
                    }
                    auto next_expanded = std::make_shared<const item_delivery::ExpandedCatalog>(
                        item_delivery::expand_catalog(next_delivery));
                    Json::object_t described;
                    for (const auto& [index, item] : next_delivery)
                    {
                        described.emplace(std::to_string(index),
                                          native_catalog::describe(next_facts.at(index), item,
                                                                   next_expanded->at(index)));
                    }
                    const auto item_json = Json(std::move(described));
                    auto encoded_items = encode(item_json);
                    const bool description_changed = encoded_items != description_items;
                    auto next_description = description_payload;
                    if (description_changed)
                    {
                        next_description = encode(Json{{"schema", "kh3ap.item_descriptions.v1"},
                                                       {"owner", "dll"},
                                                       {"process_id", GetCurrentProcessId()},
                                                       {"seed_name", seed},
                                                       {"slot", ap_slot},
                                                       {"revision", description_revision + 1},
                                                       {"items", item_json}});
                    }
                    previous_delivery = std::move(delivery_catalog);
                    previous_facts = std::move(receipt_facts);
                    delivery_catalog = std::move(next_delivery);
                    expanded = std::move(next_expanded);
                    receipt_facts = std::move(next_facts);
                    replaced = true;
                    catalog_loaded = true;
                    resolve_checks(true);
                    commit();
                    if (description_changed)
                    {
                        description_items = std::move(encoded_items);
                        description_payload = std::move(next_description);
                        receipt_descriptions = item_json.get_ref<const Json::object_t&>();
                        ++description_revision;
                    }
                    // Template updates must refresh the typed queue even if membership stayed the
                    // same.
                    delivery_signature.clear();
                }
                catch (...)
                {
                    slots = previous_slots;
                    catalog = previous_catalog;
                    active = previous_active;
                    server_checked = previous_checked;
                    received_item_ids = previous_item_ids;
                    if (replaced)
                    {
                        delivery_catalog = std::move(previous_delivery);
                        receipt_facts = std::move(previous_facts);
                        expanded = previous_expanded;
                        catalog_loaded = previous_loaded;
                    }
                    throw;
                }
                report_receipts.clear();
                uint32_t character_mask{};
                bool spellweaver_received = false;
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (item.supported)
                    {
                        character_mask |=
                            character_unlocks::token_bit(item.primary.grant.runtime_token);
                        spellweaver_received |=
                            item.primary.grant.runtime_token == aqua_style_unlock::token;
                    }
                }
                character_unlocks::received_mask.store(character_mask, std::memory_order_release);
                aqua_style_unlock::publish(spellweaver_received);
                ++receipt_catalog_revision;
                for (const auto& [index, item] : delivery_catalog)
                {
                    const auto& token = item.primary.grant.runtime_token;
                    if (!item.supported || token.size() != report_item_token_chars ||
                        !token.starts_with(report_item_prefix) || token[report_tens_index] < L'0' ||
                        token[report_tens_index] > L'9' || token[report_units_index] < L'0' ||
                        token[report_units_index] > L'9')
                    {
                        continue;
                    }
                    const auto report = (token[report_tens_index] - L'0') * 10 +
                                        token[report_units_index] - L'0' - 1;
                    if (report >= 1 && report <= secret_report_count)
                    {
                        report_receipts.emplace(index, report);
                    }
                }
                auto waiting = std::move(reobserved);
                reobserved.clear();
                for (const auto& [key, observation] : waiting)
                {
                    if (observation.epoch == processed_epoch)
                    {
                        repeat(key, observation);
                    }
                }
                initialized = true;
                update_pending();
                auto saved_jobs = std::move(before_catalog);
                before_catalog.clear();
                for (const auto& job : saved_jobs)
                {
                    apply(job);
                }
            }

        public:
            auto configure_hints(const Json& metadata) -> void
            {
                if (text(field(metadata, "seed_name")) != seed ||
                    number(field(metadata, "slot")) != ap_slot)
                {
                    throw std::runtime_error("secret_report_metadata_identity_mismatch");
                }
                Json::object_t targets;
                const auto& reports = field(metadata, "reports");
                if (!reports.is_object())
                {
                    throw std::runtime_error("invalid_secret_report_targets");
                }
                for (const auto& [key, rows] : reports.get_ref<const Json::object_t&>())
                {
                    if (key != "1" && key != "2" && key != "3" && key != "4" && key != "5" &&
                        key != "6" && key != "7" && key != "8" && key != "9" && key != "10" &&
                        key != "11" && key != "12" && key != "13")
                    {
                        throw std::runtime_error("invalid_secret_report_number");
                    }
                    if (!rows.is_array())
                    {
                        throw std::runtime_error("invalid_secret_report_targets");
                    }
                    std::set<std::pair<int64_t, int64_t>> unique;
                    for (const auto& row : array(rows))
                    {
                        const auto player = number(field(row, "player"));
                        const auto location = number(field(row, "location"));
                        if (player <= 0 || location <= 0)
                        {
                            throw std::runtime_error("invalid_secret_report_target");
                        }
                        unique.emplace(player, location);
                    }
                    Json::array_t checked;
                    for (const auto& [player, location] : unique)
                    {
                        checked.emplace_back(Json{{"player", player}, {"location", location}});
                    }
                    targets.emplace(key, Json(std::move(checked)));
                }
                hint_targets = Json(std::move(targets));
            }
            auto recover() -> void
            {
                const auto document = json::read_seed_document(root / "slot_data.json");
                const auto& value = document->value;
                const auto& identity = field(value, "kh3_seed");
                seed = text(field(identity, "seed_name"));
                ap_slot = number(field(identity, "slot"));
                if (seed.empty() || ap_slot <= 0)
                {
                    throw std::runtime_error("seed_metadata_invalid");
                }
                chest_catalog.configure(value);
                keyblade_manifest = encode(chest_catalog.keyblades(GetCurrentProcessId()));
                std::ifstream mom_clears(root / channels::runtime_mom_clear_state,
                                         std::ios::binary);
                if (mom_clears)
                {
                    const auto data = read_stream_bounded(mom_clears, json::limits::checkpoint_bytes);
                    if (!data)
                    {
                        throw std::runtime_error("checkpoint_read_failed");
                    }
                    chest_catalog.accept_clears(decode(*data));
                }
                // Optional hints must never prevent checks, receipts or save recovery.
                try
                {
                    configure_hints(*json::read_seed_json(root / "runtime_seed_hints.json"));
                }
                catch (const std::exception& e)
                {
                    hint_error = e.what();
                    hint_targets = Json::object_t{};
                }
                seed_catalog.configure(value);
                ingredient_mode = number(field(value, "ingredients"),
                                         number(field(value, "ingredient_checks"), 0));
                adversary_checks = number(field(value, "adversary_checks"), 0) == 1;
                classic_mode = number(field(value, "classic_kingdom_checks"), 0);
                coliseum_mode = number(field(value, "olympus_coliseum_pool"), 0);
                const auto& bridge = field(value, "bridge");
                if (bridge.contains("locations"))
                {
                    for (const auto& row : array(bridge["locations"]))
                    {
                        const auto key = text(field(row, "bridge_key"));
                        const auto table = text(field(row, "source_table"));
                        check_policy.locations[key] = {key, table, text(field(row, "source_id"))};
                        if (auto condition = kh02_checks::condition(row))
                        {
                            if (text(field(row, "type")) == "kh02_chest")
                            {
                                kh02_ap_chests.insert(utf8_to_wstring(
                                    condition->id.substr(std::string_view{"Treasure:"}.size())));
                            }
                            kh02_check_conditions.push_back(std::move(*condition));
                        }
                        location_ids[key] = number(field(row, "ap_location_id"));
                        const auto trigger = text(field(row, "trigger_bridge_key"));
                        if (!trigger.empty() && !row.contains("required_item_ids"))
                        {
                            extra_triggers[key] = trigger;
                        }
                        auto name = text(field(row, "location_description"));
                        if (name.empty())
                        {
                            name = text(field(row, "ap_location_name"));
                        }
                        const auto& classification = field(row, "assigned_item_classification");
                        const auto classification_text = text(classification);
                        locations[key] = {
                            .name = name,
                            .item = text(field(row, "assigned_item_name")),
                            .recipient = text(field(row, "assigned_item_player_name")),
                            .type = text(field(row, "type")),
                            .source_id = text(field(row, "source_id")),
                            .foreign = flag(field(row, "is_foreign_item")),
                            .priority = classification.is_number()
                                            ? (number(classification) & item_priority_mask) != 0
                                            : classification_text.find("progression") !=
                                                      std::string::npos ||
                                                  classification_text.find("advancement") !=
                                                      std::string::npos,
                            .always_on = text(field(row, "world")) == "Always On" ||
                                         name.starts_with("Always On - ")};
                        progress_keys[{locations[key].type, locations[key].source_id}] = key;
                        progress_keys[{locations[key].type, key}] = key;
                        // Placed Melody chests share the chest completion channel and
                        // checkpoint with ordinary treasure, while retaining their
                        // melody_of_memory location type. Alias only placed chests
                        // so both live progress and cold recovery resolve them.
                        if (locations[key].type == "melody_of_memory" && table == "MusicChest" &&
                            number(field(row, "music_chest_placement_version")) == 1)
                        {
                            progress_keys[{"treasure", locations[key].source_id}] = key;
                            progress_keys[{"treasure", key}] = key;
                        }
                    }
                }
                if (bridge.contains("conditional_checks"))
                {
                    for (const auto& rule : array(bridge["conditional_checks"]))
                    {
                        const auto start = text(field(rule, "unless_start_option"));
                        if (!start.empty() &&
                            (flag(field(value, start)) || number(field(value, start), 0) != 0))
                        {
                            continue;
                        }
                        const auto target = number(field(rule, "ap_location_id"));
                        for (const auto& [key, id] : location_ids)
                        {
                            if (id == target)
                            {
                                std::vector<int64_t> required_items;
                                if (rule.contains("required_item_ids"))
                                {
                                    for (const auto& item : array(rule["required_item_ids"]))
                                    {
                                        required_items.push_back(number(item));
                                    }
                                }
                                conditional_checks.push_back(
                                    {key, text(field(rule, "trigger_bridge_key")),
                                     number(field(rule, "unless_item_id"), -1), std::move(required_items)});
                            }
                        }
                    }
                }
                for (const auto& [file, type] :
                     std::map<std::string, std::string>{{"event_progress.json", "event"},
                                                        {"vbonus_progress.json", "vbonus"},
                                                        {"chest_progress.json", "treasure"},
                                                        {"lucky_mark_progress.json", "lucky_mark"},
                                                        {"level_up_progress.json", "level_up"}})
                {
                    std::ifstream checkpoint_file(root / file, std::ios::binary);
                    if (checkpoint_file)
                    {
                        const auto data = read_stream_bounded(checkpoint_file, json::limits::checkpoint_bytes);
                        if (!data)
                        {
                            throw std::runtime_error("checkpoint_read_failed");
                        }
                        progress(decode(*data), type, false);
                    }
                }
                std::error_code ec;
                if (!std::filesystem::exists(root / journal_file, ec))
                {
                    if (ec)
                    {
                        throw std::runtime_error("journal_path_unreadable");
                    }
                    publish();
                    return;
                }
                if (std::filesystem::file_size(root / journal_file) >
                    maximum_reconciliation_journal_bytes)
                {
                    throw std::runtime_error("journal_size_limit");
                }
                std::ifstream file(root / journal_file, std::ios::binary);
                if (!file)
                {
                    throw std::runtime_error("journal_read_failed");
                }
                const auto data = read_stream_bounded(file, maximum_reconciliation_journal_bytes);
                if (!data)
                {
                    throw std::runtime_error("journal_read_failed");
                }
                const auto j = decode(*data);
                if (text(field(j, "schema")) != "kh3ap.native_reconciliation.v4" ||
                    !flag(field(j, "complete")) || text(field(j, "seed_name")) != seed ||
                    number(field(j, "slot")) != ap_slot)
                {
                    throw std::runtime_error("journal_identity_mismatch");
                }
                accounting.restore(field(j, "receipt_accounting"));
                if (j.contains("gate_openers"))
                {
                    for (const auto& choice : array(j["gate_openers"]))
                    {
                        const auto receipt = number(field(choice, "receipt"));
                        const auto portal = number(field(choice, "portal"));
                        if (receipt < 0 || portal < gate_opener::first_portal_id ||
                            portal > gate_opener::last_portal_id ||
                            !portals::by_id(static_cast<int>(portal)) ||
                            gates.spent.contains(receipt) || gates.opened(static_cast<int>(portal)))
                        {
                            throw std::runtime_error("invalid_gate_opener_choice");
                        }
                        gates.spent.emplace(receipt, static_cast<int>(portal));
                    }
                }
                ++accounting_revision;
                committed_accounting = accounting;
                for (const auto& row : array(field(j, "slots")))
                {
                    const auto slot = number(field(row, "save_slot"));
                    if (slot < 0 || slot > max_journal_save_slot || slots.contains(slot))
                    {
                        throw std::runtime_error("invalid_journal_slot");
                    }
                    slots.emplace(slot, receipts(field(row, "saved_receipts")));
                    const auto& progress = field(row, "kh02_progress");
                    try
                    {
                        if (auto saved_progress = kh02_progress::from_json(progress))
                        {
                            kh02.slots.emplace(slot, std::move(saved_progress));
                        }
                    }
                    catch (const std::exception& e)
                    {
                        kh02_invalid_slots[slot] = e.what();
                    }
                }
                for (const auto& row : array(field(j, "catalog")))
                {
                    const auto index = number(field(row, "index"));
                    if (index < 0 || index > max_catalog_item_index || catalog.contains(index) ||
                        !field(row, "replay").is_boolean() ||
                        !field(row, "passthrough").is_boolean())
                    {
                        throw std::runtime_error("invalid_journal_catalog");
                    }
                    catalog.emplace(index, Catalog{flag(field(row, "replay")),
                                                   flag(field(row, "passthrough"))});
                }
                for (const auto& key : array(field(j, "repair_keys")))
                {
                    repairs.insert(text(key));
                }
                for (const auto& key : array(field(j, "observed_checks")))
                {
                    check_policy.observed.insert(text(key));
                }
                for (const auto& key : array(field(j, "completed_checks")))
                {
                    if (!check_policy.locations.contains(text(key)))
                    {
                        throw std::runtime_error("invalid_completed_check");
                    }
                    check_policy.completed.insert(text(key));
                    loot_policy.checked.insert(text(key));
                }
                check_policy.goal = flag(field(j, "goal_complete"));
                for (const auto& i : array(field(j, "forced_indices")))
                {
                    forced.insert(number(i));
                }
                if (!field(j, "hint_report_numbers").is_array())
                {
                    throw std::runtime_error("journal_hint_reports_missing");
                }
                for (const auto& i : array(j["hint_report_numbers"]))
                {
                    const auto report = number(i);
                    if (report < 1 || report > secret_report_count)
                    {
                        throw std::runtime_error("invalid_journal_hint_report");
                    }
                    hint_reports.insert(report);
                }
                const auto frozen = field(j, "deathlink");
                if (!frozen.is_null())
                {
                    if (text(field(frozen, "request_id")).empty() ||
                        text(field(frozen, "request_id")).size() >
                            max_reconciliation_request_id_bytes ||
                        !field(frozen, "confirmed").is_boolean() ||
                        number(field(frozen, "process_id")) <= 0)
                    {
                        throw std::runtime_error("invalid_deathlink_checkpoint");
                    }
                    (void)receipts(field(frozen, "saved_receipts"));
                    if (flag(field(frozen, "confirmed")) ||
                        number(field(frozen, "process_id")) == GetCurrentProcessId())
                    {
                        deathlink = frozen;
                    }
                }
                initialized = true;
                publish();
            }
            auto fail(std::string reason) -> void
            {
                error = std::move(reason);
                receipt_acks.clear();
                accounting = committed_accounting;
                ++accounting_revision;
                publish();
            }
            auto flush_receipt_acks() -> void
            {
                if (receipt_acks.empty())
                {
                    return;
                }
                // Coalesce completed frame slices already queued on the worker.
                // Save/load and other events remain strict ordering boundaries.
                commit();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                const auto receipt_committed = std::chrono::steady_clock::now();
#endif
                publish();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                const auto receipt_published = std::chrono::steady_clock::now();
                auto& measured_ack = receipt_acks.back();
                measured_ack["runtime_receipt_commit_elapsed_ms"] =
                    std::chrono::duration<double, std::milli>(receipt_committed - receipt_started)
                        .count();
                measured_ack["runtime_receipt_projection_elapsed_ms"] =
                    std::chrono::duration<double, std::milli>(receipt_published - receipt_committed)
                        .count();
                measured_ack["runtime_receipt_worker_elapsed_ms"] =
                    std::chrono::duration<double, std::milli>(receipt_published - receipt_started)
                        .count();
                measured_ack["runtime_receipt_batch_count"] = receipt_acks.size();
#endif
                for (const auto& ack : receipt_acks)
                {
                    pipe::write(root / channels::runtime_receipt_ack_event, encode(ack));
                    size_t notice_index{};
                    for (auto notice : array(field(ack, "client_notifications")))
                    {
                        notice["schema"] = "kh3ap.notification.v1";
                        notice["owner"] = "dll";
                        notice["process_id"] = GetCurrentProcessId();
                        notice["seed_name"] = seed;
                        notice["slot"] = ap_slot;
                        notice["delivery_epoch"] = processed_epoch;
                        notice["request_id"] = "native-receipt-notification-" +
                                               text(field(ack, "event_key")) + "|" +
                                               std::to_string(notice_index++);
                        pipe::write(root / channels::runtime_notification_event, encode(notice));
                    }
                }
                receipt_acks.clear();
            }
            // Done includes ignored/stale events and events that publish their
            // own boundary. Finalize uses apply's shared check commit/publish tail.
            enum class EventDisposition
            {
                Done,
                Finalize
            };

            auto handle_item_applied(const Job& job) -> EventDisposition
            {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                if (receipt_acks.empty())
                {
                    receipt_started = std::chrono::steady_clock::now();
                }
#endif
                const auto event = decode(job.data);
                if (!bound(event) || text(field(event, "owner")) != "dll" ||
                    number(field(event, "delivery_epoch")) != processed_epoch || !catalog_loaded ||
                    !expanded || !loaded || dead)
                {
                    return EventDisposition::Done;
                }
                if (text(field(event, "status")) == "applied" ||
                    text(field(event, "status")) == "partial")
                {
                    for (const auto& value : array(field(event, "applied_indices")))
                    {
                        const auto index = receipt_accounting::integer(value);
                        if (index < 0)
                        {
                            continue; // Native commands have no AP receipt.
                        }
                        if (!delivery_catalog.contains(index))
                        {
                            throw std::runtime_error("native_ack_unknown_receipt");
                        }
                        active.add(index);
                        pending.erase(index);
                        const auto& item = delivery_catalog.at(index);
                        const auto& grants = expanded->at(index);
                        if (inventory_tracking && inventory_valid && !grants.empty())
                        {
                            const auto mirrored = [](const auto& grant)
                            {
                                return item_delivery::writes_inventory_backup(grant.grant_method);
                            };
                            if (std::all_of(grants.begin(), grants.end(), mirrored))
                            {
                                inventory_backup.add(index);
                            }
                            else if (std::any_of(grants.begin(), grants.end(), mirrored))
                            {
                                inventory_partial.insert(index);
                            }
                        }
                        retry_preserved.erase(index);
                        if (item.bridge_required || forced.contains(index))
                        {
                            repairs.erase(item.bridge_key);
                        }
                    }
                }
                std::set<int64_t> accounted;
                for (const auto& [index, item] : delivery_catalog)
                {
                    if (active.contains(index))
                    {
                        accounted.insert(index);
                    }
                }
                if (accounting.remember(accounted, text(field(event, "applied_at"))))
                {
                    ++accounting_revision;
                }
                if (!text(field(deathlink, "request_id")).empty() &&
                    text(field(event, "request_id")) == text(field(deathlink, "request_id")) &&
                    text(field(event, "status")) == "applied" &&
                    !flag(field(deathlink, "confirmed")))
                {
                    deathlink["confirmed"] = true;
                }
                receipt_acks.push_back(
                    accounting.complete(event, accounted, receipt_descriptions,
                                        item_delivery::schedule_catalog(delivery_catalog, *expanded,
                                                                        active, forced, repairs)
                                            .pending.size()));
                ++accounting_revision;
                return EventDisposition::Done;
            }

            auto handle_kh02_progress(const Job& job) -> EventDisposition
            {
                if (!job.kh02_update ||
                    !kh02_progress::accepts(job.kh02_update->binding, seed, ap_slot, kh02.revision,
                                            processed_epoch, loaded, dead))
                {
                    return EventDisposition::Done;
                }
                try
                {
                    if (!kh02.update(job.kh02_update->snapshot, job.kh02_update->binding.revision))
                    {
                        return EventDisposition::Done;
                    }
                }
                catch (const std::exception& error)
                {
                    kh02_error = error.what();
                    publish();
                    return EventDisposition::Done;
                }
                for (const auto& condition : kh02_check_conditions)
                {
                    if (kh02_checks::earned(kh02.active, condition))
                    {
                        check_policy.observe("KH02", condition.id);
                    }
                }
                resolve_checks(true);
                // Working progress is captured only by an actual save boundary.
                // Publish the new in-process bank for subsequent map entry.
                return EventDisposition::Finalize;
            }

            auto handle_kh02_reset(const Job& job) -> EventDisposition
            {
                if (!job.kh02_reset ||
                    !kh02_progress::accepts(*job.kh02_reset, seed, ap_slot, kh02.revision,
                                            processed_epoch, loaded, dead))
                {
                    return EventDisposition::Done;
                }
                // Local chapter replay has its own revision. AP receipts and
                // acknowledged checks remain monotonic across that replay.
                kh02.new_game();
                kh02_error.clear();
                return EventDisposition::Finalize;
            }

            auto handle_inventory_capture(const Job& job) -> EventDisposition
            {
                if (!job.success)
                {
                    throw std::runtime_error("native_inventory_snapshot_inside_item_grant");
                }
                // Same FIFO as mutation ACKs: this copy is synchronous on
                // the game thread. Never sample a later published View.
                inventory_backup = active;
                inventory_valid = true;
                std::erase_if(inventory_partial,
                              [&](auto index)
                              {
                                  return active.contains(index);
                              });
                baseline = checkpoint(inventory_backup, loaded_slot, "native_inventory_snapshot",
                                      utc(job.time));
                return EventDisposition::Finalize;
            }

            auto handle_inventory_restore(const Job& job) -> EventDisposition
            {
                if (!job.success)
                {
                    throw std::runtime_error("native_inventory_restore_inside_item_grant");
                }
                if (!loaded)
                {
                    return EventDisposition::Done;
                }
                if (!inventory_valid)
                {
                    throw std::runtime_error("native_inventory_snapshot_missing");
                }
                runtime_status.clear();
                processed_epoch = ++current_epoch;
                dead = true;
                inventory_restored = true;
                retry_preserved = inventory_partial;
                rollback(inventory_backup, "native_inventory_restore", utc(job.time));
                return EventDisposition::Finalize;
            }

            auto handle_autosave_begin(const Job& job, bool& mutated) -> EventDisposition
            {
                // Repeated native requests before the write replace the pending
                // payload boundary. Only an observed save operation consumes it;
                // asynchronous serialization/I/O has no receipt expiry time.
                pending_autosave = Save{0, active, utc(job.time), loaded && !dead, kh02.active};
                mutated = false;
                return EventDisposition::Finalize;
            }

            auto handle_save_load(const Job& job) -> EventDisposition
            {
                runtime_status.clear();
                processed_epoch = job.epoch;
                loaded_slot = job.slot;
                loaded = true;
                dead = false;
                retry_valid = false;
                pawn.clear();
                kh02.load(job.slot);
                kh02_error =
                    kh02_invalid_slots.contains(job.slot) ? kh02_invalid_slots.at(job.slot) : "";
                inventory_restored = false;
                inventory_valid = false;
                inventory_partial.clear();
                retry_preserved.clear();
                disk_checkpoint = slots.contains(job.slot) ? slots.at(job.slot) : Receipts{};
                context = nullptr;
                rollback(disk_checkpoint, "load_continue_retry", utc(job.time));
                current =
                    checkpoint(disk_checkpoint, job.slot, "receipt_snapshot_load", utc(job.time));
                last_load = current;
                last_load["access_kind"] = "load";
                pending_autosave.reset();
                saves.clear();
                return EventDisposition::Finalize;
            }

            auto handle_save_open(const Job& job, bool& mutated) -> EventDisposition
            {
                if (job.success && saves.contains(job.event))
                {
                    return EventDisposition::Done;
                }
                if (saves.size() >= max_save_transactions)
                {
                    throw std::runtime_error("save_transaction_capacity");
                }
                Save entry{job.slot, active, utc(job.time), loaded && !dead, kh02.active};
                if (job.slot == 0)
                {
                    entry.correlated = false;
                    if (pending_autosave)
                    {
                        entry = std::move(*pending_autosave);
                    }
                    // No pre-serialization proof: retain the prior saved
                    // checkpoint, never infer serialized grants from a poll.
                }
                // Bind once to this open-event identity. Failed opens/writes,
                // later manual saves and repeated closes must not reuse an old
                // request. An already-open save owns its independent snapshot.
                pending_autosave.reset();
                if (job.success)
                {
                    saves.emplace(job.event, std::move(entry));
                }
                mutated = false;
                return EventDisposition::Finalize;
            }

            auto handle_save_closed(const Job& job) -> EventDisposition
            {
                const auto found = saves.find(job.event);
                if (found == saves.end())
                {
                    return EventDisposition::Done;
                }
                const auto saved = found->second;
                saves.erase(found);
                if (!job.success || !saved.correlated)
                {
                    return EventDisposition::Done;
                }
                const auto previous_slots = slots;
                const auto previous_kh02_slots = kh02.slots;
                slots[saved.slot] = saved.snapshot;
                kh02.slots[saved.slot] = saved.kh02_progress;
                try
                {
                    commit();
                }
                catch (...)
                {
                    slots = previous_slots;
                    kh02.slots = previous_kh02_slots;
                    throw;
                }
                if (saved.slot == 0)
                {
                    disk_checkpoint = saved.snapshot;
                    current =
                        checkpoint(saved.snapshot, 0, "save_file_open_polyhook_autosave", saved.at);
                }
                else
                {
                    pipe::write(root / channels::runtime_display_save_slot,
                                encode(checkpoint(saved.snapshot, saved.slot, "native_manual_save",
                                                  saved.at)));
                }
                return EventDisposition::Finalize;
            }

            auto handle_new_game(const Job& job) -> EventDisposition
            {
                runtime_status.clear();
                pending_autosave.reset();
                saves.clear();
                processed_epoch = job.epoch;
                kh02.new_game();
                kh02_error.clear();
                context = decode(job.data);
                loaded = true;
                dead = false;
                retry_valid = false;
                loaded_slot = 0;
                inventory_restored = false;
                inventory_valid = false;
                inventory_partial.clear();
                retry_preserved.clear();
                disk_checkpoint = {};
                rollback({}, "new_game_staging", utc());
                current = checkpoint({}, 0, "new_game_staging", recovery_at);
                last_load = nullptr;
                return EventDisposition::Finalize;
            }

            auto handle_current_state_write(const Job& job) -> EventDisposition
            {
                const auto event = decode(job.data);
                const auto state =
                    text(field(event, "state_class")) + text(field(event, "new_state_class"));
                if (state.find("GameOver") == std::string::npos ||
                    !flag(field(event, "state_comp_matches_cached_pawn")))
                {
                    return EventDisposition::Done;
                }
                if (dead)
                {
                    return EventDisposition::Done;
                }
                runtime_status.clear();
                processed_epoch = ++current_epoch;
                death_map = map_path(context);
                dead = true;
                messages.clear();
                reobserved.clear();
                death_key = text(field(event, "recorded_at_utc"));
                return EventDisposition::Finalize;
            }

            auto handle_runtime_context(const Job& job, size_t previous_observed,
                                        bool previous_goal) -> EventDisposition
            {
                const auto incoming = decode(job.data);
                const bool map_ready = job.kind == channels::runtime_map_ready;
                if (map_ready && text(field(incoming, "schema")) != "kh3ap.runtime_map_ready.v1")
                {
                    return EventDisposition::Done;
                }
                const auto object = text(field(incoming, "object"));
                const auto map = map_path(incoming);
                const auto source = text(field(incoming, "source"));
                const auto opened_at =
                    text(field(incoming, map_ready ? "observed_at_utc" : "opened_at"));
                if (job.epoch != processed_epoch || source == "recovered_from_ue4ss_log" ||
                    source == "migrated_previous_bridge_context" ||
                    object.find("real=false") != std::string::npos)
                {
                    return EventDisposition::Done;
                }
                if (loaded && !dead)
                {
                    const auto encounter = checks::Policy::encounter(
                        text(field(incoming, "hook")),
                        text(field(incoming, "world")) + "\n" + text(field(incoming, "outer")));
                    if (!encounter.empty())
                    {
                        check_policy.observe("VBonus", encounter);
                        resolve_checks(true);
                    }
                    check_policy.goal |= checks::Policy::ending(map);
                }
                if (text(field(incoming, "hook")) != "RuntimePlayablePawnContext" ||
                    object.empty() || map.empty())
                {
                    if (check_policy.goal != previous_goal ||
                        check_policy.observed.size() != previous_observed)
                    {
                        commit();
                    }
                    publish();
                    return EventDisposition::Done;
                }
                context = incoming;
                const bool room_entry = map_ready || text(field(incoming, "pawn_source")) ==
                                                         "VanillaLevelWritePolyhook:level_owner";
                if (dead && (object != pawn || room_entry))
                {
                    // A delayed map-name callback can arrive during Game
                    // Over, before Retry restores the native inventory.
                    // Pawn discovery/room entry alone cannot end that wait.
                    // Keep receipts and chest assignments intact until the
                    // authoritative restore (or a separate disk load).
                    if (inventory_tracking && !inventory_restored)
                    {
                        publish();
                        return EventDisposition::Done;
                    }
                    dead = false;
                    if (inventory_tracking)
                    {
                        inventory_restored = false;
                        pawn = object;
                        publish();
                        return EventDisposition::Done;
                    }
                    // Prepare & Retry can reuse Sora, and its first context
                    // may arrive only after the encounter transitions onward.
                    // Bind recovery to the death's room, not that destination.
                    const auto r = flag(field(deathlink, "confirmed"))
                                       ? receipts(field(deathlink, "saved_receipts"))
                                   : retry_valid && death_map == retry_map ? retry_checkpoint
                                                                           : disk_checkpoint;
                    rollback(r, "death_continue_retry", opened_at);
                }
                else if (!inventory_tracking && loaded && room_entry &&
                         (!retry_valid || map != retry_map || object != retry_pawn))
                {
                    // A preceding cached-pawn publication must not consume
                    // this boundary; duplicate map-ready events must not
                    // advance it past items granted inside the encounter.
                    retry_checkpoint = active;
                    retry_map = map;
                    retry_pawn = object;
                    retry_valid = true;
                    baseline =
                        checkpoint(retry_checkpoint, loaded_slot, "sora_zone_entry", opened_at);
                    baseline["runtime_level_path"] = map;
                    baseline["cutoff_index"] = retry_checkpoint.through;
                }
                pawn = object;
                return EventDisposition::Finalize;
            }

            auto handle_gate_opener(const Job& job) -> void
            {
                const auto expected = decode(job.data);
                if (!loaded || dead || job.epoch != processed_epoch ||
                    text(field(expected, "seed")) != seed ||
                    number(field(expected, "slot")) != ap_slot)
                {
                    return;
                }
                auto previous = gates;
                if (!gates.consume(static_cast<int>(job.slot), gate_receipts()))
                {
                    return;
                }
                try
                {
                    commit();
                }
                catch (...)
                {
                    gates = std::move(previous);
                    throw;
                }
                publish();
                return;
            }
            auto handle_runtime_status(const Job& job) -> EventDisposition
            {
                if (job.epoch != processed_epoch)
                {
                    return EventDisposition::Done;
                }
                const auto input = decode(job.data);
                const auto source = text(field(input, "source"));
                if (source == "recovered_from_ue4ss_log" ||
                    source == "migrated_previous_bridge_context" ||
                    (input.contains("process_id") &&
                     number(input["process_id"]) != GetCurrentProcessId()) ||
                    text(field(input, "object")).find("real=false") != std::string::npos)
                {
                    return EventDisposition::Done;
                }
                runtime_status.observe(job.kind, input);
                if (job.kind != channels::runtime_context &&
                    job.kind != channels::runtime_map_ready)
                {
                    publish_runtime_status();
                    return EventDisposition::Done;
                }

                return EventDisposition::Finalize;
            }
            auto handle_trap_queue_ack(const Job& job) -> void
            {
                const auto ack = decode(job.data);
                if (!bound(ack) ||
                    !text(field(ack, "request_id")).starts_with("native-delivery-traps-"))
                {
                    return;
                }
                if (text(field(ack, "status")) != "accepted")
                {
                    throw std::runtime_error("native_trap_admission_failed");
                }
                for (const auto& i : array(field(ack, "indices")))
                {
                    const auto index = number(i);
                    if (!submitted_traps.contains(index))
                    {
                        throw std::runtime_error("trap_ack_receipt_mismatch");
                    }
                    catalog.at(index).passthrough = true;
                    active.add(index);
                    pending.erase(index);
                }
                commit();
                publish();
                return;
            }
            auto handle_persistent_reward_notice(const Job& job) -> EventDisposition
            {
                if (job.epoch != processed_epoch)
                {
                    return EventDisposition::Done;
                }
                if (messages.size() >= notification_queue_capacity)
                {
                    throw std::runtime_error("notification_queue_capacity");
                }
                const auto notice = decode(job.data);
                messages.push_back(RuntimeApMessageRequest{
                    .request_id = utf8_to_wstring(text(field(notice, "id"))),
                    .message = utf8_to_wstring(text(field(notice, "message")))});

                return EventDisposition::Finalize;
            }
            auto handle_message_completed(const Job& job) -> EventDisposition
            {
                if (job.epoch != processed_epoch || messages.empty() ||
                    wstring_to_utf8(messages.front().request_id) != job.data)
                {
                    return EventDisposition::Done;
                }
                messages.pop_front();

                return EventDisposition::Finalize;
            }
            auto handle_classic_progress(const Job& job) -> EventDisposition
            {
                const auto input = decode(job.data);
                if (text(field(input, "schema")) != "kh3ap.runtime_classic_kingdom_state.v3" ||
                    classic_mode < 1 ||
                    number(field(input, "classic_kingdom_checks")) != classic_mode)
                {
                    return EventDisposition::Done;
                }
                table_progress(input, "ClassicKingdomHighScore", "high_score_games");

                return EventDisposition::Finalize;
            }
            auto handle_coliseum_progress(const Job& job) -> EventDisposition
            {
                const auto input = decode(job.data);
                if (text(field(input, "schema")) != "kh3ap.runtime_olympus_coliseum_state.v1" ||
                    coliseum_mode < 1 ||
                    number(field(input, "olympus_coliseum_pool")) != coliseum_mode)
                {
                    return EventDisposition::Done;
                }
                table_progress(input, "OlympusColiseumCup", "completed_cups");

                return EventDisposition::Finalize;
            }
            auto handle_space_progress(const Job& job) -> EventDisposition
            {
                const auto input = decode(job.data);
                if (text(field(input, "schema")) != "kh3ap.space_checks.v1" ||
                    text(field(input, "source")) != "native_space_completion")
                {
                    return EventDisposition::Done;
                }
                table_progress(input, "GummiSpace", "completed");

                return EventDisposition::Finalize;
            }
            auto handle_minigame_progress(const Job& job) -> EventDisposition
            {
                const auto input = decode(job.data);
                if (text(field(input, "schema")) != "kh3ap.minigame_rewards.v1" ||
                    text(field(input, "source")) != "remuneration_reward")
                {
                    return EventDisposition::Done;
                }
                table_progress(input, "MinigameReward", "completed_rewards");

                return EventDisposition::Finalize;
            }
            auto handle_ingredient_progress(const Job& job) -> EventDisposition
            {
                const auto input = decode(job.data);
                if (text(field(input, "schema")) != "kh3ap.runtime_ingredient_state.v1" ||
                    text(field(input, "source")) != "food_prize" ||
                    ingredient_mode < ingredient_items_mode ||
                    ingredient_mode > ingredient_spots_mode)
                {
                    return EventDisposition::Done;
                }
                table_progress(input,
                               ingredient_mode == ingredient_spots_mode ? "IngredientSpot"
                                                                        : "Ingredient",
                               ingredient_mode == ingredient_spots_mode ? "acquired_spots"
                                                                        : "acquired_ingredients");

                return EventDisposition::Finalize;
            }
            auto handle_adversary_progress(const Job& job) -> EventDisposition
            {
                const auto input = decode(job.data);
                if (!adversary_checks || !loaded || dead || job.epoch != processed_epoch ||
                    number(field(input, "process_id")) != GetCurrentProcessId() ||
                    text(field(input, "schema")) != "kh3ap.runtime_adversary_state.v1" ||
                    text(field(input, "source")) != "native_kill_counters")
                {
                    return EventDisposition::Done;
                }
                table_progress(input, "Adversary", "defeated");

                return EventDisposition::Finalize;
            }
            auto handle_workshop_progress(const Job& job) -> EventDisposition
            {
                const auto input = decode(job.data);
                if (text(field(input, "schema")) != "kh3ap.workshop.v1" ||
                    text(field(input, "source")) != "native_workshop_flags" ||
                    text(field(input, "seed_name")) != seed ||
                    number(field(input, "slot")) != ap_slot)
                {
                    return EventDisposition::Done;
                }
                for (const auto& raw : array(field(input, "completed_checks")))
                {
                    const auto key = text(raw);
                    const auto found = check_policy.locations.find(key);
                    if (found != check_policy.locations.end())
                    {
                        check_policy.observe(found->second.table, found->second.id);
                    }
                }
                resolve_checks(true);

                return EventDisposition::Finalize;
            }
            auto handle_deathlink_begin(const Job& job) -> EventDisposition
            {
                if (!loaded || dead)
                {
                    return EventDisposition::Done;
                }
                if (text(field(deathlink, "request_id")) == job.data)
                {
                    return EventDisposition::Done;
                }
                if (!deathlink.is_null())
                {
                    throw std::runtime_error("deathlink_fence_conflict");
                }
                const auto r = inventory_tracking && inventory_valid           ? inventory_backup
                               : retry_valid && map_path(context) == retry_map ? retry_checkpoint
                                                                               : disk_checkpoint;
                deathlink = Json{{"request_id", job.data},
                                 {"process_id", GetCurrentProcessId()},
                                 {"confirmed", false},
                                 {"saved_receipts", receipt_json(r)}};
                try
                {
                    commit();
                }
                catch (...)
                {
                    deathlink = nullptr;
                    throw;
                }

                return EventDisposition::Finalize;
            }
            auto handle_deathlink_end(const Job& job) -> EventDisposition
            {
                if (text(field(deathlink, "request_id")) != job.data)
                {
                    return EventDisposition::Done;
                }
                const auto previous = deathlink;
                deathlink = nullptr;
                try
                {
                    commit();
                }
                catch (...)
                {
                    deathlink = previous;
                    throw;
                }

                return EventDisposition::Finalize;
            }
            auto dispatch_event(const Job& job, bool& mutated, size_t previous_observed,
                                bool previous_goal) -> EventDisposition
            {
                struct Handler
                {
                    std::string_view channel;
                    EventDisposition (State::*apply)(const Job&);
                };
                static constexpr auto handlers = std::to_array<Handler>({
                    {"persistent_reward_notice", &State::handle_persistent_reward_notice},
                    {"message_completed", &State::handle_message_completed},
                    {channels::runtime_classic_kingdom_state, &State::handle_classic_progress},
                    {channels::runtime_olympus_coliseum_state, &State::handle_coliseum_progress},
                    {channels::runtime_space_checks, &State::handle_space_progress},
                    {channels::runtime_minigame_rewards, &State::handle_minigame_progress},
                    {channels::runtime_ingredient_state, &State::handle_ingredient_progress},
                    {channels::runtime_adversary_state, &State::handle_adversary_progress},
                    {channels::runtime_workshop, &State::handle_workshop_progress},
                    {"deathlink_begin", &State::handle_deathlink_begin},
                    {"deathlink_end", &State::handle_deathlink_end},
                    {channels::runtime_item_apply_event, &State::handle_item_applied},
                    {"kh02_progress", &State::handle_kh02_progress},
                    {"kh02_reset", &State::handle_kh02_reset},
                    {"inventory_capture", &State::handle_inventory_capture},
                    {"inventory_restore", &State::handle_inventory_restore},
                    {"load", &State::handle_save_load},
                    {"save_closed", &State::handle_save_closed},
                    {channels::runtime_new_game_staging, &State::handle_new_game},
                    {channels::current_state_write_polyhook_events,
                     &State::handle_current_state_write},
                });
                for (const auto& handler : handlers)
                {
                    if (job.kind == handler.channel)
                    {
                        return (this->*handler.apply)(job);
                    }
                }
                static constexpr std::array check_channels{
                    std::pair{channels::runtime_event_checks, "event"},
                    std::pair{channels::runtime_vbonus_checks, "vbonus"},
                    std::pair{channels::runtime_chest_checks, "treasure"},
                    std::pair{channels::runtime_lucky_mark_checks, "lucky_mark"},
                    std::pair{channels::runtime_level_up_checks, "level_up"},
                };
                for (const auto& [channel, type] : check_channels)
                {
                    if (job.kind == channel)
                    {
                        progress(decode(job.data), type, true);
                        return EventDisposition::Finalize;
                    }
                }
                if (job.kind == channels::runtime_loot_event)
                {
                    loot(job);
                }
                else if (job.kind == "inventory_observer")
                {
                    inventory_tracking = job.success;
                }
                else if (job.kind == "auto_begin")
                {
                    return handle_autosave_begin(job, mutated);
                }
                else if (job.kind == "save_open")
                {
                    return handle_save_open(job, mutated);
                }
                else if (job.kind == channels::runtime_context ||
                         job.kind == channels::runtime_map_ready)
                {
                    return handle_runtime_context(job, previous_observed, previous_goal);
                }
                else
                {
                    return EventDisposition::Done;
                }
                return EventDisposition::Finalize;
            }

            auto apply(const Job& job) -> void
            {
                if (!error.empty())
                {
                    return;
                }
                if (job.kind == "gate_opener")
                {
                    handle_gate_opener(job);
                    return;
                }
                if (runtime_status::Snapshot::channel(job.kind))
                {
                    if (handle_runtime_status(job) == EventDisposition::Done)
                    {
                        return;
                    }
                }
                if (job.kind == channels::runtime_receipt_catalog)
                {
                    metadata(decode(job.data));
                    publish();
                    return;
                }
                if (job.kind == channels::runtime_mom_clear_state)
                {
                    if (chest_catalog.accept_clears(decode(job.data)))
                    {
                        chest_manifest_dirty = true;
                        publish();
                    }
                    return;
                }
                if (job.kind == channels::runtime_trap_queue_ack)
                {
                    handle_trap_queue_ack(job);
                    return;
                }
                if (!initialized)
                {
                    if (before_catalog.size() >= catalog_event_capacity)
                    {
                        throw std::runtime_error("catalog_event_overflow");
                    }
                    before_catalog.push_back(job);
                    publish();
                    return;
                }
                bool mutated = true;
                const auto previous_observed = check_policy.observed.size();
                const auto previous_completed = check_policy.completed.size();
                const auto previous_goal = check_policy.goal;
                const auto previous_forced = forced;
                if (dispatch_event(job, mutated, previous_observed, previous_goal) ==
                    EventDisposition::Done)
                {
                    return;
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                if (job.kind == "inventory_capture" || job.kind == "inventory_restore")
                {
                    // Agent evidence only, emitted by this worker after the
                    // ordered receipt boundary. Never access vanilla saves.
                    std::ofstream trace(root / "runtime_inventory_snapshot_events.jsonl",
                                        std::ios::app);
                    Json::array_t bonus_counts;
                    for (auto n : job.bonuses)
                    {
                        bonus_counts.emplace_back(n);
                    }
                    trace << encode(Json{{"kind", job.kind},
                                         {"at", utc(job.time)},
                                         {"process_id", GetCurrentProcessId()},
                                         {"seed_name", seed},
                                         {"backup_receipts", receipt_json(inventory_backup)},
                                         {"active_receipts", receipt_json(active)},
                                         {"dead", dead},
                                         {"delivery_epoch", processed_epoch},
                                         {"source_bonus_counts", std::move(bonus_counts)}})
                          << '\n';
                }
#endif
                if (check_policy.observed.size() != previous_observed ||
                    check_policy.completed.size() != previous_completed ||
                    check_policy.goal != previous_goal || forced != previous_forced)
                {
                    commit();
                }
                if (mutated)
                {
                    publish();
                }
            }
        };

        auto run() -> void
        {
            State state;
            try
            {
                state.recover();
            }
            catch (const std::exception& e)
            {
                state.fail(e.what());
            }
            for (;;)
            {
                std::vector<Job> batch;
                {
                    std::unique_lock lock{mutex};
                    changed.wait(lock,
                                 []
                                 {
                                     return stopping || !jobs.empty() || overflow.load();
                                 });
                    if (jobs.empty() && stopping)
                    {
                        break;
                    }
                    if (overflow.exchange(false))
                    {
                        lock.unlock();
                        state.fail("native_event_queue_overflow");
                        continue;
                    }
                    batch.push_back(std::move(jobs.front()));
                    jobs.pop_front();
                    if (batch.front().kind == channels::runtime_item_apply_event)
                    {
                        while (!jobs.empty() &&
                               jobs.front().kind == channels::runtime_item_apply_event &&
                               batch.size() < item_apply_batch_capacity)
                        {
                            batch.push_back(std::move(jobs.front()));
                            jobs.pop_front();
                        }
                    }
                }
                try
                {
                    for (const auto& job : batch)
                    {
                        state.apply(job);
                    }
                    state.flush_receipt_acks();
                }
                catch (const std::exception& e)
                {
                    state.fail(e.what());
                }
            }
        }
    } // namespace
    auto start(const std::filesystem::path& path) -> void
    {
        std::lock_guard lock{mutex};
        if (worker.joinable())
        {
            return;
        }
        root = path;
        stopping = false;
        running = true;
        jobs.clear();
        current_epoch.store(1);
        overflow.store(false);
        character_unlocks::received_mask.store(0, std::memory_order_release);
        aqua_style_unlock::publish(false);
        snapshot.store(std::make_shared<const View>());
        worker = std::thread(run);
    }
    auto stop() -> void
    {
        {
            std::lock_guard lock{mutex};
            if (!worker.joinable())
            {
                return;
            }
            stopping = true;
            changed.notify_one();
        }
        worker.join();
        std::lock_guard lock{mutex};
        running = false;
        character_unlocks::received_mask.store(0, std::memory_order_release);
        aqua_style_unlock::publish(false);
    }
    auto view() -> std::shared_ptr<const View>
    {
        return snapshot.load();
    }
    auto epoch() -> int64_t
    {
        return current_epoch.load();
    }
    auto open_gate(int portal, const View& expected) -> void
    {
        if (!expected.ready || !portals::by_id(portal))
        {
            return;
        }
        enqueue(Job{
            .kind = "gate_opener",
            .data = encode(Json{{"seed", wstring_to_utf8(expected.seed)}, {"slot", expected.slot}}),
            .slot = portal,
            .epoch = expected.epoch});
    }
    auto observe(std::string_view channel, std::string_view json) -> void
    {
        if (!runtime_status::Snapshot::channel(channel) &&
            channel != channels::runtime_receipt_catalog && channel != channels::runtime_context &&
            channel != channels::runtime_map_ready && channel != channels::runtime_trap_queue_ack &&
            channel != channels::runtime_mom_clear_state &&
            channel != channels::runtime_classic_kingdom_state &&
            channel != channels::runtime_olympus_coliseum_state &&
            channel != channels::runtime_space_checks &&
            channel != channels::runtime_minigame_rewards &&
            channel != channels::runtime_ingredient_state &&
            channel != channels::runtime_adversary_state && channel != channels::runtime_workshop &&
            channel != channels::runtime_loot_event && channel != channels::runtime_event_checks &&
            channel != channels::runtime_vbonus_checks &&
            channel != channels::runtime_chest_checks &&
            channel != channels::runtime_lucky_mark_checks &&
            channel != channels::runtime_level_up_checks &&
            channel != channels::current_state_write_polyhook_events &&
            channel != channels::runtime_new_game_staging &&
            channel != channels::runtime_item_apply_event)
        {
            return;
        }
        if (channel == channels::runtime_new_game_staging)
        {
            ++current_epoch;
        }
        enqueue(Job{.kind = std::string(channel),
                    .data = std::string(json),
                    .epoch = current_epoch.load()});
    }
    auto auto_save_begin(uint64_t filetime) -> void
    {
        enqueue(Job{.kind = "auto_begin", .time = filetime});
    }
    auto kh02_progress_update(kh02_progress::Snapshot progress, uint64_t revision,
                              std::wstring_view seed, int64_t player_slot) -> void
    {
        if (revision > INT64_MAX)
        {
            return;
        }
        kh02_progress::Update update{
            {wstring_to_utf8(seed), player_slot, revision, current_epoch.load()},
            kh02_progress::freeze(std::move(progress))};
        // Do not coalesce progress jobs: an intermediate completed challenge may
        // be observed before a reset, and native save boundaries share this FIFO.
        enqueue(Job{.kind = "kh02_progress", .kh02_update = std::move(update)});
    }
    auto kh02_progress_reset(uint64_t revision, std::wstring_view seed, int64_t player_slot) -> void
    {
        if (revision > INT64_MAX)
        {
            return;
        }
        kh02_progress::Binding identity{wstring_to_utf8(seed), player_slot, revision,
                                        current_epoch.load()};
        enqueue(Job{.kind = "kh02_reset", .kh02_reset = std::move(identity)});
    }
    auto inventory_snapshot_observer(bool enabled) -> void
    {
        inventory_observer = enabled;
        enqueue(Job{.kind = "inventory_observer", .success = enabled});
    }
    auto inventory_snapshot(bool restored, uint64_t filetime, bool outside_item_grant,
                            std::array<uint8_t, vbonus_observation_capacity> bonuses) -> void
    {
        enqueue(Job{.kind = restored ? "inventory_restore" : "inventory_capture",
                    .time = filetime,
                    .success = outside_item_grant,
                    .bonuses = bonuses});
    }
    auto file_open(std::wstring_view path, uint32_t access, uint64_t event, uint64_t filetime,
                   bool succeeded) -> void
    {
        std::wstring name{path};
        std::transform(name.begin(), name.end(), name.begin(),
                       [](wchar_t c)
                       {
                           return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
                       });
        const auto start = name.rfind(save_slot_file_prefix);
        if (start == std::wstring::npos || !name.ends_with(save_slot_file_suffix))
        {
            return;
        }
        int64_t slot = 0;
        const auto begin = start + save_slot_file_prefix.size();
        if (begin >= name.size() - save_slot_file_suffix.size())
        {
            return;
        }
        for (auto i = begin; i < name.size() - save_slot_file_suffix.size(); ++i)
        {
            if (name[i] < L'0' || name[i] > L'9')
            {
                return;
            }
            slot = slot * 10 + name[i] - L'0';
            if (slot > max_journal_save_slot)
            {
                return;
            }
        }
        if (access & GENERIC_WRITE)
        {
            enqueue(Job{.kind = "save_open",
                        .slot = slot,
                        .event = event,
                        .time = filetime,
                        .success = succeeded});
        }
        else if (succeeded && (access & GENERIC_READ))
        {
            ++current_epoch;
            enqueue(Job{.kind = "load",
                        .slot = slot,
                        .epoch = current_epoch.load(),
                        .event = event,
                        .time = filetime});
            pipe::notify_grants();
        }
    }
    auto file_closed(uint64_t event, bool success) -> void
    {
        enqueue(Job{.kind = "save_closed", .event = event, .success = success});
    }
    auto deathlink_begin(std::string_view id) -> void
    {
        enqueue(Job{.kind = "deathlink_begin", .data = std::string(id)});
    }
    auto deathlink_end(std::string_view id) -> void
    {
        enqueue(Job{.kind = "deathlink_end", .data = std::string(id)});
    }
    auto persistent_reward_notice(const RuntimeItemGrantRequest& request, int64_t delivery_epoch)
        -> void
    {
        const auto id =
            request.request_id + L":persistent-reward:" + std::to_wstring(request.index);
        const auto message = kh02rewards::reward_notice(request.grant_method, request.item_name);
        enqueue(Job{.kind = "persistent_reward_notice",
                    .data = encode(
                        Json{{"id", wstring_to_utf8(id)}, {"message", wstring_to_utf8(message)}}),
                    .epoch = delivery_epoch});
    }
    auto message_completed(std::wstring_view id, int64_t delivery_epoch) -> void
    {
        enqueue(
            Job{.kind = "message_completed", .data = wstring_to_utf8(id), .epoch = delivery_epoch});
    }
} // namespace kh3ap::reconciliation
