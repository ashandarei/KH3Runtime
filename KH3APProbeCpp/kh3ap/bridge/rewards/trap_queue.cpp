#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/serialization/request_parser.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace kh3ap::native_traps
{
    namespace
    {
        constexpr auto maximum_trap_journal_bytes = 4 * 1024 * 1024;

        constexpr auto first_printable_identity_character = 32;
        constexpr auto max_trap_request_grants = 4096;
        constexpr auto max_pending_traps = 4096;
        constexpr auto trap_job_queue_capacity = 64;

        struct Entry { RuntimeItemGrantRequest request; bool applied{}; };
        struct Job { std::shared_ptr<const RuntimeItemGrantBatchRequest> request; int64_t applied{-1}; std::string reason; };
        std::atomic<std::shared_ptr<const View>> snapshot{std::make_shared<const View>()};
        std::atomic<int64_t> finishing{-1};
        std::mutex jobs_mutex;
        std::condition_variable jobs_changed;
        std::deque<Job> jobs;
        bool stopping{};
        std::thread worker;
        std::filesystem::path root;
        std::wstring seed;
        int64_t slot{};
        std::map<int64_t, Entry> ledger;
        std::string failure;
        std::string last_reason;

        auto quote_utf16(std::wstring_view value) -> std::string
        {
            const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string utf8(size, '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), size, nullptr, nullptr);
            std::string out = "\"";
            for (const auto ch : utf8)
            {
                if (ch == '\\' || ch == '\"') out += '\\';
                if (static_cast<unsigned char>(ch) < first_printable_identity_character) throw std::runtime_error("invalid_identity_text");
                out += ch;
            }
            return out + "\"";
        }
        auto read_disk(const std::filesystem::path& path) -> std::optional<std::string>
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
            {
                if (ec) throw std::runtime_error("journal_path_unreadable");
                return {};
            }
            if (std::filesystem::file_size(path) > maximum_trap_journal_bytes)
            {
                throw std::runtime_error("journal_size_limit");
            }
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("journal_read_failed");
            auto data = read_stream_bounded(file, maximum_trap_journal_bytes);
            if (!data) throw std::runtime_error("journal_read_failed");
            return data;
        }
        auto serialize(const std::map<int64_t, Entry>& entries) -> std::string
        {
            std::string out = "{\"schema\":\"kh3ap.native_trap_queue.v1\",\"seed_name\":" + quote_utf16(seed) +
                ",\"slot\":" + std::to_string(slot) + ",\"record_count\":" + std::to_string(entries.size()) + ",\"traps\":[";
            bool first = true;
            for (const auto& [index, entry] : entries)
            {
                if (!first) out += ',';
                first = false;
                out += "{\"index\":" + std::to_string(index) + ",\"kh3_runtime_token\":" + quote_utf16(entry.request.runtime_token) +
                    ",\"kh3_grant_method\":" + quote_utf16(entry.request.grant_method) + ",\"request_id\":" + quote_utf16(L"native-trap-" + std::to_wstring(index)) +
                    ",\"item_name\":" + quote_utf16(find_runtime_trap_spec(entry.request.runtime_token, entry.request.grant_method)->name) + ",\"action_kind\":\"deferred_bad_status_trap\",\"applied\":" +
                    (entry.applied ? "true" : "false") + "}";
            }
            return out + "],\"complete\":true}";
        }
        auto commit(const std::map<int64_t, Entry>& entries) -> void
        {
            const auto data = serialize(entries);
            if (data.size() > maximum_trap_journal_bytes)
            {
                throw std::runtime_error("journal_size_limit");
            }
            const auto destination = root / journal_file;
            const auto temporary = std::filesystem::path(destination.wstring() + L".tmp");
            HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("journal_open_failed");
            DWORD written{};
            const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                written == data.size() && FlushFileBuffers(file);
            CloseHandle(file);
            if (!ok || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("journal_commit_failed");
        }
        auto publish(std::string reason = {}) -> void
        {
            auto next = std::make_shared<View>();
            next->ready = failure.empty() && !seed.empty() && slot > 0;
            next->seed = seed; next->slot = slot; next->error = failure; next->accepted = ledger.size();
            for (const auto& [index, entry] : ledger)
                if (!entry.applied)
                {
                    ++next->pending;
                    if (!next->head) next->head = entry.request;
                }
            if (!next->ready || finishing.load() >= 0) next->head.reset();
            snapshot.store(next, std::memory_order_release);
            const auto safe_reason = failure.empty() ? reason : failure;
            pipe::write(root / state_file, "{\"schema\":\"kh3ap.native_trap_queue_state.v1\",\"owner\":\"dll\",\"seed_name\":" + quote_utf16(seed) +
                ",\"slot\":" + std::to_string(slot) + ",\"pending_trap_count\":" + std::to_string(next->pending) +
                ",\"accepted_trap_count\":" + std::to_string(next->accepted) + ",\"ready\":" + (next->ready ? "true" : "false") +
                ",\"reason\":\"" + safe_reason + "\"}");
            if (next->head) pipe::notify_grants();
        }
        auto acknowledge(const RuntimeItemGrantBatchRequest& request, std::string status, std::string error) -> void
        {
            std::string indices = "[";
            for (const auto& grant : request.grants)
            {
                if (indices.size() > 1) indices += ',';
                indices += std::to_string(grant.index);
            }
            indices += ']';
            pipe::write(root / ack_file, "{\"protocol_name\":\"kh3_ap_file_bridge\",\"protocol_version\":1,"
                "\"schema\":\"kh3ap.native_trap_queue_ack.v1\",\"request_id\":" + quote_utf16(request.request_id) +
                ",\"process_id\":" + std::to_string(GetCurrentProcessId()) + ",\"seed_name\":" + quote_utf16(request.seed_name) +
                ",\"slot\":" + std::to_string(request.slot) + ",\"status\":\"" + status + "\",\"reason\":\"" + error +
                "\",\"indices\":" + indices + "}");
        }
        auto recover() -> void
        {
            const auto document = json::read_seed_document(root / L"slot_data.json");
            const auto* metadata = &document->text;
            const auto identity = JsonObject{*metadata}.object_field("kh3_seed");
            seed = identity.string_field("seed_name").value_or(L"");
            slot = identity.int64_field("slot").value_or(0);
            if (seed.empty() || slot <= 0) throw std::runtime_error("seed_metadata_invalid");
            const auto saved = read_disk(root / journal_file);
            if (!saved) return;
            if (JsonFieldReader::string_field(*saved, "schema") != L"kh3ap.native_trap_queue.v1" ||
                JsonFieldReader::string_field(*saved, "seed_name") != seed || JsonFieldReader::int64_field(*saved, "slot") != slot)
                throw std::runtime_error("journal_identity_mismatch");
            const auto entries = JsonFieldReader::object_array_field(*saved, "traps");
            if (JsonFieldReader::bool_field(*saved, "complete") != true ||
                JsonFieldReader::int64_field(*saved, "record_count") != static_cast<int64_t>(entries.size()))
                throw std::runtime_error("journal_invalid");
            for (const auto& text : entries)
            {
                auto request = RuntimeRequestParser::parse_item_grant(text);
                const auto done = JsonFieldReader::bool_field(text, "applied");
                if (!request || !is_trap(*request) || !done || ledger.contains(request->index))
                    throw std::runtime_error("journal_invalid_entry");
                ledger.emplace(request->index, Entry{std::move(*request), *done});
            }
        }
        auto run() -> void
        {
            try { recover(); } catch (const std::exception& error) { failure = error.what(); }
            publish();
            for (;;)
            {
                Job job;
                {
                    std::unique_lock lock{jobs_mutex};
                    jobs_changed.wait(lock, [] { return stopping || !jobs.empty(); });
                    if (jobs.empty() && stopping) break;
                    job = std::move(jobs.front()); jobs.pop_front();
                }
                try
                {
                    if (job.request && !job.reason.empty())
                    {
                        acknowledge(*job.request, "rejected", job.reason);
                        continue;
                    }
                    if (!failure.empty()) throw std::runtime_error(failure);
                    if (job.request)
                    {
                        const auto& request = *job.request;
                        if (request.request_schema != L"kh3ap.item_delivery.v1" || request.process_id != GetCurrentProcessId())
                            throw std::runtime_error("request_process_mismatch");
                        if (request.seed_name != seed || request.slot != slot) throw std::runtime_error("request_seed_mismatch");
                        if (request.grants.empty() || request.grants.size() > max_trap_request_grants) throw std::runtime_error("request_capacity");
                        auto next = ledger;
                        for (const auto& grant : request.grants)
                        {
                            if (!is_trap(grant)) throw std::runtime_error("unsupported_trap");
                            const auto found = next.find(grant.index);
                            if (found != next.end() && found->second.request.runtime_token != grant.runtime_token)
                                throw std::runtime_error("receipt_identity_conflict");
                            next.try_emplace(grant.index, Entry{grant});
                        }
                        if (std::count_if(next.begin(), next.end(), [](const auto& e) { return !e.second.applied; }) > max_pending_traps)
                            throw std::runtime_error("queue_capacity");
                        if (next.size() != ledger.size()) commit(next);
                        ledger = std::move(next);
                        acknowledge(request, "accepted", "durably_queued");
                        publish("durably_queued");
                    }
                    else if (job.applied >= 0)
                    {
                        auto next = ledger;
                        next.at(job.applied).applied = true;
                        commit(next); ledger = std::move(next);
                        finishing.store(-1);
                        publish("applied");
                    }
                    else if (job.reason != last_reason)
                    {
                        last_reason = job.reason; publish(job.reason);
                    }
                }
                catch (const std::exception& error)
                {
                    if (job.request) acknowledge(*job.request, "rejected", error.what());
                    else { failure = error.what(); publish(); }
                }
            }
        }
        auto queue(Job job) -> void
        {
            {
                std::lock_guard lock{jobs_mutex};
                if (jobs.size() < trap_job_queue_capacity || job.applied >= 0)
                {
                    jobs.push_back(std::move(job)); jobs_changed.notify_one(); return;
                }
            }
            if (job.request) acknowledge(*job.request, "rejected", "admission_capacity");
        }
    }
    auto is_trap(const RuntimeItemGrantRequest& request) -> bool
    {
        return request.index >= 0 && find_runtime_trap_spec(request.runtime_token, request.grant_method);
    }
    auto start(const std::filesystem::path& directory) -> void
    {
        if (worker.joinable()) return;
        root = directory; ledger.clear(); seed.clear(); slot = 0; failure.clear(); last_reason.clear();
        stopping = false; finishing.store(-1); snapshot.store(std::make_shared<const View>());
        worker = std::thread(run);
    }
    auto stop() -> void
    {
        if (!worker.joinable()) return;
        { std::lock_guard lock{jobs_mutex}; stopping = true; }
        jobs_changed.notify_one(); worker.join();
    }
    auto view() -> std::shared_ptr<const View>
    {
        auto result = snapshot.load(std::memory_order_acquire);
        if (finishing.load() < 0) return result;
        auto copy = std::make_shared<View>(*result); copy->head.reset(); return copy;
    }
    auto submit(std::shared_ptr<const RuntimeItemGrantBatchRequest> request) -> void { queue(Job{.request = std::move(request)}); }
    auto reject(std::shared_ptr<const RuntimeItemGrantBatchRequest> request, std::string reason) -> void
    { queue(Job{.request = std::move(request), .reason = std::move(reason)}); }
    auto applied(int64_t index) -> void
    {
        int64_t expected = -1;
        if (finishing.compare_exchange_strong(expected, index)) queue(Job{.applied = index});
    }
    auto waiting(std::string reason) -> void { queue(Job{.reason = std::move(reason)}); }
}
