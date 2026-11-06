#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/session/deathlink.h"
#include "kh3ap/bridge/session/deathlink_policy.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/features/player/player_state_ids.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace kh3ap::deathlink
{
    namespace
    {
        constexpr auto deathlink_synthetic_item_id = -9001;

        constexpr auto max_deathlink_key_bytes = 128;
        constexpr auto max_deathlink_id_bytes = 160;
        constexpr auto max_deathlink_amnesty = 20;
        constexpr auto max_deathlink_source_bytes = 1024;
        constexpr auto max_deathlink_cause_bytes = 4096;
        constexpr auto deathlink_job_capacity = 4096;
        constexpr auto max_exact_json_integer = 9007199254740991.0;

        using Json = kh3ap::json::Value;
        struct Job
        {
            std::string channel, data;
        };
        std::atomic<std::shared_ptr<const View>> snapshot{std::make_shared<const View>()};
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<Job> jobs;
        bool running{}, stopping{}, overflow{};
        std::thread worker;
        std::filesystem::path root;
        constexpr size_t history_limit = 4096, outbox_limit = 256;

        auto field(const Json& j, std::string_view key) -> const Json&
        {
            static const Json nil;
            return j.contains(key) ? j[key] : nil;
        }
        auto text(const Json& j) -> std::string
        {
            return j.is_string() ? j.get_ref<const std::string&>() : "";
        }
        auto flag(const Json& j) -> bool
        {
            return j.is_boolean() && j.get<bool>();
        }
        auto integer(const Json& j) -> int64_t
        {
            if (!j.is_number())
            {
                throw std::runtime_error("invalid_integer");
            }
            const auto n = j.get<double>();
            if (!std::isfinite(n) || std::floor(n) != n || std::abs(n) > max_exact_json_integer)
            {
                throw std::runtime_error("invalid_integer");
            }
            return static_cast<int64_t>(n);
        }
        auto array(const Json& j) -> const Json::array_t&
        {
            if (!j.is_array())
            {
                throw std::runtime_error("invalid_array");
            }
            return j.get_ref<const Json::array_t&>();
        }
        auto encode(const Json& j) -> std::string
        {
            std::string out;
            kh3ap::json::write(j, out);
            return out;
        }
        auto decode(std::string_view data) -> Json
        {
            Json j;
            if (!kh3ap::json::parse(j, data))
            {
                throw std::runtime_error("invalid_json");
            }
            return j;
        }
        auto read_disk(const std::filesystem::path& path) -> std::optional<Json>
        {
            if (!std::filesystem::exists(path))
            {
                return {};
            }
            if (std::filesystem::file_size(path) > json::limits::checkpoint_bytes)
            {
                throw std::runtime_error("json_size_limit");
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("checkpoint_read_failed");
            }
            const auto data = read_stream_bounded(input, json::limits::checkpoint_bytes);
            if (!data)
            {
                throw std::runtime_error("checkpoint_read_failed");
            }
            return decode(*data);
        }
        auto commit(const Json& j) -> void
        {
            const auto data = encode(j);
            const auto destination = root / journal_file;
            const auto temporary = std::filesystem::path(destination.wstring() + L".tmp");
            HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                throw std::runtime_error("checkpoint_open_failed");
            }
            DWORD written{};
            const bool ok =
                WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                written == data.size() && FlushFileBuffers(file);
            CloseHandle(file);
            if (!ok || !MoveFileExW(temporary.c_str(), destination.c_str(),
                                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                throw std::runtime_error("checkpoint_commit_failed");
            }
        }
        auto strings(const Json& value, size_t limit) -> std::deque<std::string>
        {
            const auto& values = array(value);
            if (values.size() > limit)
            {
                throw std::runtime_error("history_capacity_exceeded");
            }
            std::deque<std::string> result;
            std::set<std::string> unique;
            for (const auto& entry : values)
            {
                const auto id = text(entry);
                if (id.empty() || id.size() > max_deathlink_id_bytes || !unique.insert(id).second)
                {
                    throw std::runtime_error("invalid_history");
                }
                result.push_back(id);
            }
            return result;
        }
        auto string_array(const std::deque<std::string>& values) -> Json
        {
            Json::array_t out;
            for (const auto& v : values)
            {
                out.emplace_back(v);
            }
            return out;
        }
        auto contains(const std::deque<std::string>& values, const std::string& value) -> bool
        {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        class State
        {
            std::string seed, error;
            int64_t slot{}, episode{};
            bool enabled{}, online{}, connected{}, dead{}, state_known{}, safe{}, received_seen{};
            int64_t amnesty{}, forgiven{}, order{}, death_order{}, context_order{},
                dispatch_order{}, dispatched_epoch{-1};
            Json request, context;
            std::string reconciled_context;
            bool release_sent{};
            bool station_of_awakening{};
            std::string manual_dead_player;
            int64_t manual_death_order{};
            bool manual_player_recovered{};
            std::deque<std::string> accepted;
            Json::array_t outgoing;
            DWORD recovered_process{};
            auto pending() const -> bool
            {
                return text(field(request, "status")) == "pending";
            }
            auto recovering() const -> bool
            {
                return text(field(request, "status")) == "applied";
            }
            auto checkpoint() const -> Json
            {
                return Json{{"schema", "kh3ap.native_deathlink.v1"},
                            {"owner", "dll"},
                            {"seed_name", seed},
                            {"slot", slot},
                            {"enabled", enabled},
                            {"amnesty", amnesty},
                            {"forgiven_deaths", forgiven},
                            {"episode", episode},
                            {"request", request},
                            {"accepted_ids", string_array(accepted)},
                            {"outgoing", outgoing},
                            {"complete", true}};
            }
            auto update(const std::function<void()>& mutate) -> void
            {
                const State previous = *this;
                try
                {
                    mutate();
                    commit(checkpoint());
                }
                catch (...)
                {
                    *this = previous;
                    throw;
                }
            }
            auto scope(const Json& j) const -> bool
            {
                try
                {
                    return text(field(j, "seed_name")) == seed &&
                           integer(field(j, "slot")) == slot &&
                           integer(field(j, "process_id")) == GetCurrentProcessId();
                }
                catch (const std::exception&)
                {
                    return false;
                }
            }
            auto ready_context() const -> bool
            {
                const auto object = text(field(context, "object"));
                return text(field(context, "hook")) == "RuntimePlayablePawnContext" &&
                       object.find("real=true") != std::string::npos &&
                       object.find("real=false") == std::string::npos &&
                       !text(field(context, "world")).empty() &&
                       !text(field(context, "opened_at")).empty() &&
                       text(field(context, "source")) != "recovered_from_ue4ss_log" &&
                       text(field(context, "source")) != "migrated_previous_bridge_context";
            }
            auto maybe_recover() -> void
            {
                if (!recovering() || !reconciliation::view()->ready)
                {
                    return;
                }
                const bool manual_recovered = flag(field(request, "manual")) && received_seen &&
                                              manual_player_recovered &&
                                              recovered_process == GetCurrentProcessId();
                if (!manual_recovered && (!state_known || dead || !safe || !ready_context() ||
                                          reconciled_context != encode(context)))
                {
                    return;
                }
                // In-process recovery needs an owned Game Over and a newer
                // playable context. A cold process also needs native load /
                // receipt recovery before its safe Sora context can release it.
                if (!manual_recovered && recovered_process == GetCurrentProcessId() &&
                    (!received_seen || context_order <= death_order))
                {
                    return;
                }
                const auto fence = reconciliation::view()->deathlink_request;
                if (!fence.empty())
                {
                    if (fence != text(field(request, "request_id")))
                    {
                        throw std::runtime_error("reconciliation_fence_mismatch");
                    }
                    if (!release_sent)
                    {
                        release_sent = true;
                        reconciliation::deathlink_end(fence);
                    }
                    return;
                }
                update(
                    [&]
                    {
                        request["status"] = "recovered";
                        request["recovery_process_id"] = GetCurrentProcessId();
                    });
                received_seen = false;
                pipe::erase(root / channels::runtime_item_grant_priority);
                pipe::notify_grants();
            }
            auto dispatch() -> void
            {
                if (!pending() || !error.empty() || station_of_awakening)
                {
                    return;
                }
                const auto reconciliation_state = reconciliation::view();
                if (!reconciliation_state->ready)
                {
                    return;
                }
                const auto epoch = reconciliation_state->epoch;
                if (epoch != reconciliation::epoch())
                {
                    return;
                }
                const auto id = text(field(request, "request_id"));
                if (reconciliation_state->deathlink_request != id)
                {
                    reconciliation::deathlink_begin(id);
                    return;
                }
                const auto existing = pipe::read(root / channels::runtime_item_grant_priority);
                if (existing && dispatched_epoch == epoch)
                {
                    return;
                }
                Json action{{"index", deathlink_synthetic_item_id},
                            {"location_id", deathlink_synthetic_item_id},
                            {"item_name", "DeathLink Game Over"},
                            {"location_name", "DeathLink"},
                            {"from_player_name", text(field(request, "source"))},
                            {"kh3_runtime_token", "GAMEOVER_KIND_NORMAL"},
                            {"kh3_grant_method", "bp_tres_start_game_over"},
                            {"action_kind", flag(field(request, "manual"))
                                                ? "manual_game_over"
                                                : "deathlink_game_over"}};
                Json batch{{"request_schema", "kh3ap.item_delivery.v1"},
                           {"process_id", GetCurrentProcessId()},
                           {"seed_name", seed},
                           {"slot", slot},
                           {"delivery_epoch", epoch},
                           {"request_id", text(field(request, "request_id"))},
                           {"action_kind", "runtime_item_grant_batch"},
                           {"reapply_context_key", "deathlink_game_over"},
                           {"show_info", false},
                           {"grants", Json::array_t{std::move(action)}}};
                if (!pipe::submit_native_grant(root / channels::runtime_item_grant_priority,
                                               encode(batch)))
                {
                    throw std::runtime_error("native_grant_submission_failed");
                }
                dispatched_epoch = epoch;
                dispatch_order = order;
            }
            auto control(const Json& j) -> void
            {
                if (text(field(j, "schema")) != "kh3ap.deathlink_control.v1" || !scope(j))
                {
                    return;
                }
                const auto next_amnesty = integer(field(j, "amnesty"));
                if (next_amnesty < 0 || next_amnesty > max_deathlink_amnesty ||
                    !field(j, "enabled").is_boolean() || !field(j, "online").is_boolean())
                {
                    throw std::runtime_error("invalid_control_settings");
                }
                const auto next_enabled = flag(field(j, "enabled"));
                if (next_enabled != enabled || next_amnesty != amnesty)
                {
                    update(
                        [&]
                        {
                            enabled = next_enabled;
                            amnesty = next_amnesty;
                            forgiven = 0;
                            if (!enabled)
                            {
                                outgoing.clear();
                            }
                        });
                }
                connected = true;
                online = flag(field(j, "online"));
                const auto acked = strings(field(j, "sent_ids"), history_limit);
                if (std::any_of(outgoing.begin(), outgoing.end(),
                                [&](const auto& e)
                                {
                                    return contains(acked, text(field(e, "id")));
                                }))
                {
                    update(
                        [&]
                        {
                            std::erase_if(outgoing,
                                          [&](const auto& e)
                                          {
                                              return contains(acked, text(field(e, "id")));
                                          });
                        });
                }
                const auto& incoming = array(field(j, "incoming"));
                if (incoming.size() > outbox_limit)
                {
                    throw std::runtime_error("incoming_capacity_exceeded");
                }
                for (const auto& intent : incoming)
                {
                    const auto id = text(field(intent, "request_id"));
                    if (id.empty() || id.size() > max_deathlink_id_bytes ||
                        !field(intent, "source").is_string() ||
                        !field(intent, "cause").is_string() ||
                        text(field(intent, "source")).size() > max_deathlink_source_bytes ||
                        text(field(intent, "cause")).size() > max_deathlink_cause_bytes ||
                        (intent.contains("manual") && !field(intent, "manual").is_boolean()))
                    {
                        throw std::runtime_error("invalid_incoming_intent");
                    }
                    if (contains(accepted, id))
                    {
                        continue;
                    }
                    update(
                        [&]
                        {
                            accepted.push_back(id);
                            if (accepted.size() > history_limit)
                            {
                                accepted.pop_front();
                            }
                            // One received death owns the transition; additional
                            // packets are durably coalesced, including reconnects.
                            // Explicit local /gameover commands use the same native
                            // transition without requiring AP DeathLink or a server.
                            if (!station_of_awakening &&
                                (flag(field(intent, "manual")) || (enabled && online)) &&
                                !pending() && !recovering())
                            {
                                request = Json{{"request_id", id},
                                               {"status", "pending"},
                                               {"source", text(field(intent, "source"))},
                                               {"cause", text(field(intent, "cause"))},
                                               {"manual", flag(field(intent, "manual"))},
                                               {"process_id", GetCurrentProcessId()}};
                                recovered_process = GetCurrentProcessId();
                                received_seen = release_sent = false;
                                dispatched_epoch = -1;
                                manual_dead_player.clear();
                                manual_player_recovered = false;
                                manual_death_order = 0;
                            }
                        });
                }
            }
            auto manual_player(const Json& j) -> void
            {
                if (!flag(field(request, "manual")) || (!pending() && !recovering()) ||
                    integer(field(j, "process_id")) != GetCurrentProcessId())
                {
                    return;
                }
                const auto key = text(field(j, "player_key"));
                if (key.empty() || key.size() > max_deathlink_key_bytes)
                {
                    return;
                }
                if (flag(field(j, "dead")))
                {
                    manual_dead_player = key;
                    manual_player_recovered = false;
                    dead = true;
                    death_order = order;
                    manual_death_order = order;
                    if (recovering())
                    {
                        received_seen = true;
                    }
                }
                else if (flag(field(j, "ready")) && !manual_dead_player.empty() &&
                         key != manual_dead_player)
                {
                    manual_player_recovered = true;
                    dead = false;
                    maybe_recover();
                }
            }
            auto sora(const Json& j) -> void
            {
                if (field(j, "runtime_context").is_string())
                {
                    station_of_awakening = is_station_of_awakening(
                        std::string_view{text(field(j, "runtime_context"))});
                }
                if (text(field(j, "status")) != "known")
                {
                    return;
                }
                const bool first = !state_known;
                state_known = true;
                const auto klass = text(field(j, "state_class"));
                const bool next_dead =
                    (field(j, "current_state_id").is_number() &&
                     integer(field(j, "current_state_id")) == player_state_ids::game_over) ||
                    klass == "TresPlayerState_GameOver" ||
                    klass.ends_with(".TresPlayerState_GameOver");
                // Special battles allow Game Over and Retry. Their mode flag
                // must not keep an observed death fenced after playable Sora returns.
                safe = text(field(j, "safety")) == "ok";
                if (!next_dead)
                {
                    dead = false;
                    maybe_recover();
                    return;
                }
                if (dead)
                {
                    return;
                }
                dead = true;
                death_order = order;
                if (recovering() || pending())
                {
                    if (recovering())
                    {
                        received_seen = true;
                    }
                    return;
                }
                if (first || station_of_awakening || !enabled || !online || !connected)
                {
                    return;
                }
                update(
                    [&]
                    {
                        ++episode;
                        if (forgiven < amnesty)
                        {
                            ++forgiven;
                        }
                        else
                        {
                            if (outgoing.size() >= outbox_limit)
                            {
                                throw std::runtime_error("outgoing_capacity_exceeded");
                            }
                            outgoing.emplace_back(
                                Json{{"id", "deathlink-local-" + std::to_string(episode)},
                                     {"cause", "Sora hit Game Over."}});
                            forgiven = 0;
                        }
                    });
            }

        public:
            auto recover() -> void
            {
                const auto metadata = json::read_seed_json(root / "slot_data.json");
                if (!metadata)
                {
                    throw std::runtime_error("seed_metadata_missing");
                }
                const auto& identity = field(*metadata, "kh3_seed");
                seed = text(field(identity, "seed_name"));
                slot = integer(field(identity, "slot"));
                if (seed.empty() || slot <= 0)
                {
                    throw std::runtime_error("seed_metadata_invalid");
                }
                const auto saved = read_disk(root / journal_file);
                if (!saved)
                {
                    return;
                }
                if (text(field(*saved, "schema")) != "kh3ap.native_deathlink.v1" ||
                    text(field(*saved, "owner")) != "dll" ||
                    text(field(*saved, "seed_name")) != seed ||
                    integer(field(*saved, "slot")) != slot || !flag(field(*saved, "complete")))
                {
                    throw std::runtime_error("checkpoint_identity_mismatch");
                }
                if (!field(*saved, "enabled").is_boolean())
                {
                    throw std::runtime_error("invalid_checkpoint_settings");
                }
                const auto a = integer(field(*saved, "amnesty")),
                           f = integer(field(*saved, "forgiven_deaths")),
                           e = integer(field(*saved, "episode"));
                if (a < 0 || a > max_deathlink_amnesty || f < 0 || f > a || e < 0)
                {
                    throw std::runtime_error("invalid_checkpoint_count");
                }
                const auto history = strings(field(*saved, "accepted_ids"), history_limit);
                const auto outbox = array(field(*saved, "outgoing"));
                if (outbox.size() > outbox_limit)
                {
                    throw std::runtime_error("outgoing_capacity_exceeded");
                }
                std::set<std::string> unique;
                for (const auto& item : outbox)
                {
                    if (text(field(item, "id")).empty() ||
                        text(field(item, "id")).size() > max_deathlink_id_bytes ||
                        text(field(item, "cause")).empty() ||
                        text(field(item, "cause")).size() > max_deathlink_cause_bytes ||
                        !unique.insert(text(field(item, "id"))).second)
                    {
                        throw std::runtime_error("invalid_checkpoint_outbox");
                    }
                }
                const auto saved_request = field(*saved, "request");
                const auto status = text(field(saved_request, "status"));
                if (!saved_request.is_null() &&
                    (text(field(saved_request, "request_id")).empty() ||
                     !contains(history, text(field(saved_request, "request_id"))) ||
                     (status != "pending" && status != "applied" && status != "recovered") ||
                     integer(field(saved_request, "process_id")) <= 0 ||
                     integer(field(saved_request, "process_id")) > UINT32_MAX ||
                     !field(saved_request, "source").is_string() ||
                     text(field(saved_request, "source")).size() > max_deathlink_source_bytes ||
                     !field(saved_request, "cause").is_string() ||
                     text(field(saved_request, "cause")).size() > max_deathlink_cause_bytes ||
                     (saved_request.contains("manual") &&
                      !field(saved_request, "manual").is_boolean())))
                {
                    throw std::runtime_error("invalid_checkpoint_request");
                }
                enabled = flag(field(*saved, "enabled"));
                amnesty = a;
                forgiven = f;
                episode = e;
                accepted = history;
                outgoing = outbox;
                request = saved_request;
                if (!request.is_null())
                {
                    recovered_process = static_cast<DWORD>(integer(field(request, "process_id")));
                }
            }
            auto publish() const -> void
            {
                auto next = std::make_shared<View>();
                next->ready = error.empty() && !seed.empty() && slot > 0;
                next->awaiting_apply = pending();
                next->recovery_pending = recovering();
                next->manual_active = flag(field(request, "manual")) && (pending() || recovering());
                next->fenced = !error.empty() || next->awaiting_apply || next->recovery_pending;
                next->error = error;
                snapshot.store(next, std::memory_order_release);
                auto j = checkpoint();
                j["schema"] = "kh3ap.deathlink_state.v1";
                j["process_id"] = GetCurrentProcessId();
                j["ready"] = next->ready;
                j["active"] = next->fenced;
                j["reason"] = error.empty() ? pending() ? "deathlink_game_over_apply_pending"
                                              : recovering()
                                                  ? "deathlink_game_over_transition_pending"
                                                  : "ready"
                                            : error;
                j["status"] = text(field(request, "status"));
                j["request_id"] = text(field(request, "request_id"));
                j["awaiting_apply"] = next->awaiting_apply;
                j["recovery_pending"] = next->recovery_pending;
                pipe::write(root / state_file, encode(j));
            }
            auto fail(std::string reason) -> void
            {
                error = std::move(reason);
                publish();
            }
            auto apply(const Job& job) -> void
            {
                ++order;
                const bool was_fenced = snapshot.load(std::memory_order_acquire)->fenced;
                if (job.channel == "disconnect")
                {
                    connected = online = false;
                    return;
                }
                if (!error.empty())
                {
                    return;
                }
                const auto j = decode(job.data);
                if (job.channel == channels::runtime_deathlink_control)
                {
                    control(j);
                }
                else if (job.channel == "manual_player")
                {
                    manual_player(j);
                }
                else if (job.channel == channels::runtime_sora_state)
                {
                    sora(j);
                }
                else if (job.channel == channels::runtime_context)
                {
                    context = j;
                    context_order = order;
                    const auto map = text(field(j, "world")) + " " + text(field(j, "outer")) + " " +
                                     text(field(j, "object"));
                    station_of_awakening = is_station_of_awakening(std::string_view{map});
                    maybe_recover();
                }
                else if (job.channel == channels::runtime_reconciliation_state)
                {
                    reconciled_context = encode(field(j, "runtime_context"));
                    maybe_recover();
                }
                else if (job.channel == channels::runtime_item_apply_event)
                {
                    if (pending() &&
                        text(field(j, "request_id")) == text(field(request, "request_id")) &&
                        text(field(j, "status")) == "applied")
                    {
                        update(
                            [&]
                            {
                                request["status"] = "applied";
                                request["process_id"] = GetCurrentProcessId();
                            });
                        recovered_process = GetCurrentProcessId();
                        // The pawn's death notification can reach this worker
                        // before the grant acknowledgement. An unrelated Sora
                        // sidecar must not erase the owned alternate's evidence.
                        received_seen =
                            (dead && death_order > dispatch_order) ||
                            (flag(field(request, "manual")) && manual_death_order > dispatch_order);
                        maybe_recover();
                    }
                }
                dispatch();
                publish();
                if (snapshot.load(std::memory_order_acquire)->fenced != was_fenced)
                {
                    pipe::notify_grants();
                }
            }
        };
        auto run() -> void
        {
            State state;
            try
            {
                state.recover();
                state.publish();
            }
            catch (const std::exception& e)
            {
                state.fail(e.what());
            }
            for (;;)
            {
                Job job;
                {
                    std::unique_lock lock{mutex};
                    changed.wait(lock,
                                 []
                                 {
                                     return stopping || overflow || !jobs.empty();
                                 });
                    if (jobs.empty() && stopping)
                    {
                        break;
                    }
                    if (overflow)
                    {
                        overflow = false;
                        lock.unlock();
                        state.fail("native_event_queue_overflow");
                        continue;
                    }
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                try
                {
                    state.apply(job);
                }
                catch (const std::exception& e)
                {
                    state.fail(e.what());
                }
            }
        }
        auto enqueue(Job job) -> void
        {
            std::lock_guard lock{mutex};
            if (!running || stopping)
            {
                return;
            }
            if (jobs.size() >= deathlink_job_capacity)
            {
                overflow = true;
            }
            else
            {
                jobs.push_back(std::move(job));
            }
            changed.notify_one();
        }
    } // namespace
    auto start(const std::filesystem::path& path) -> void
    {
        std::lock_guard lock{mutex};
        if (running)
        {
            return;
        }
        root = path;
        jobs.clear();
        stopping = overflow = false;
        running = true;
        snapshot.store(std::make_shared<const View>());
        worker = std::thread(run);
    }
    auto stop() -> void
    {
        {
            std::lock_guard lock{mutex};
            if (!running)
            {
                return;
            }
            stopping = true;
            changed.notify_one();
        }
        worker.join();
        std::lock_guard lock{mutex};
        running = false;
    }
    auto observe(std::string_view channel, std::string_view json) -> void
    {
        if (channel == channels::runtime_deathlink_control ||
            channel == channels::runtime_sora_state || channel == channels::runtime_context ||
            channel == channels::runtime_item_apply_event ||
            channel == channels::runtime_reconciliation_state)
        {
            enqueue({std::string(channel), std::string(json)});
        }
    }
    auto disconnected() -> void
    {
        enqueue({"disconnect", ""});
    }
    auto observe_manual_player(std::string player_key, bool dead, bool ready) -> void
    {
        if (!view()->manual_active)
        {
            return;
        }
        enqueue({"manual_player", encode(Json{{"process_id", GetCurrentProcessId()},
                                              {"player_key", std::move(player_key)},
                                              {"dead", dead},
                                              {"ready", ready}})});
    }
    auto view() -> std::shared_ptr<const View>
    {
        return snapshot.load(std::memory_order_acquire);
    }
} // namespace kh3ap::deathlink
