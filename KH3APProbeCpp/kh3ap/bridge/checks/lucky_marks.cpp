#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/bridge/checks/lucky_marks.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#include "kh3ap/bridge/serialization/json.h"
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace kh3ap::lucky_marks
{
    namespace
    {
        constexpr auto item_token_category_digits = 2;
        constexpr auto item_token_index_digits = 3;
        constexpr auto item_token_id_digits = item_token_category_digits + item_token_index_digits;
        constexpr auto item_token_suffix_offset = item_token_id_digits + 1; // Underscore separator.

        constexpr auto lucky_mark_total = 90;
        constexpr auto max_lucky_mark_category = 19;
        constexpr auto max_lucky_mark_index = 255;
        constexpr auto lucky_mark_pending_capacity = 4096;
        constexpr auto max_exact_json_integer = 9007199254740991.0;

        using Json = kh3ap::json::Value;
        std::atomic<std::shared_ptr<const View>> snapshot{std::make_shared<const View>()};
        std::atomic<uint64_t> snapshot_revision{};
        std::mutex mutex;
        std::condition_variable changed;
        std::set<int64_t> pending;
        bool running{}, stopping{}, overflow{};
        std::thread worker;
        std::filesystem::path root;

        auto field(const Json& j, std::string_view key) -> const Json&
        {
            static const Json nil;
            return j.contains(key) ? j[key] : nil;
        }
        auto text(const Json& j) -> std::string
        {
            return j.is_string() ? j.get_ref<const std::string&>() : "";
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
        auto read_disk(const std::filesystem::path& path) -> std::optional<Json>
        {
            // Absence is the only condition allowed to create empty history.
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
            Json j;
            if (!data || !kh3ap::json::parse(j, *data))
            {
                throw std::runtime_error("invalid_json");
            }
            return j;
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
        class State
        {
            std::string seed, error;
            int64_t slot{};
            std::map<int64_t, std::string> locations;
            std::set<std::string> selected, completed;

        public:
            auto checkpoint(const std::set<std::string>& checks) const -> Json
            {
                Json::array_t ids;
                for (const auto& id : checks)
                {
                    ids.emplace_back(id);
                }
                return Json{{"schema", "kh3ap.lucky_mark_progress.v1"},
                            {"owner", "dll"},
                            {"seed_name", seed},
                            {"slot", slot},
                            {"completed", std::move(ids)},
                            {"record_count", checks.size()},
                            {"complete", true}};
            }
            auto publish() const -> void
            {
                auto next = std::make_shared<View>();
                next->ready = error.empty() && !seed.empty() && slot > 0;
                next->error = error;
                next->bridge = root;
                next->seed = json::utf8_to_wide(seed).value_or(L"");
                next->slot = slot;
                if (next->ready)
                {
                    for (const auto& id : completed)
                    {
                        next->milestones.push_back(std::stoi(id));
                    }
                }
                snapshot.store(next, std::memory_order_release);
                snapshot_revision.fetch_add(1, std::memory_order_release);
                auto j = checkpoint(completed);
                j["process_id"] = GetCurrentProcessId();
                j["ready"] = next->ready;
                j["reason"] = error;
                pipe::write(root / state_file, encode(j));
            }
            auto recover() -> void
            {
                const auto metadata = json::read_seed_json(root / L"slot_data.json");
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
                for (const auto& row : array(field(field(*metadata, "bridge"), "locations")))
                {
                    if (text(field(row, "source_table")) != "LuckyMark")
                    {
                        continue;
                    }
                    const auto id = text(field(row, "source_id"));
                    // Milestones are the canonical decimal values 1 through 90.
                    const auto token_id =
                        location_id(L"ITEM_AP_I02200_" + std::wstring(id.begin(), id.end()));
                    if (!token_id || *token_id > lucky_mark_total ||
                        std::to_string(*token_id) != id ||
                        text(field(row, "bridge_key")) != "LuckyMark:" + id)
                    {
                        throw std::runtime_error("invalid_milestone");
                    }
                    const auto ap_id = integer(field(row, "ap_location_id"));
                    if (ap_id <= 0 || !selected.insert(id).second ||
                        !locations.emplace(ap_id, id).second)
                    {
                        throw std::runtime_error("duplicate_or_invalid_location");
                    }
                }
                const auto saved = read_disk(root / journal_file);
                if (!saved)
                {
                    return;
                }
                if (text(field(*saved, "schema")) != "kh3ap.lucky_mark_progress.v1" ||
                    text(field(*saved, "owner")) != "dll" ||
                    text(field(*saved, "seed_name")) != seed ||
                    integer(field(*saved, "slot")) != slot ||
                    field(*saved, "complete") != Json(true))
                {
                    throw std::runtime_error("checkpoint_identity_mismatch");
                }
                const auto& ids = array(field(*saved, "completed"));
                if (integer(field(*saved, "record_count")) != static_cast<int64_t>(ids.size()) ||
                    ids.size() > lucky_mark_total)
                {
                    throw std::runtime_error("invalid_checkpoint_count");
                }
                std::set<std::string> restored;
                for (const auto& value : ids)
                {
                    const auto id = text(value);
                    if (!selected.contains(id) || !restored.insert(id).second)
                    {
                        throw std::runtime_error("invalid_checkpoint_milestone");
                    }
                }
                completed = std::move(restored);
            }
            auto fail(std::string reason) -> void
            {
                error = std::move(reason);
                publish();
            }
            auto apply(const std::set<int64_t>& ids) -> void
            {
                if (!error.empty())
                {
                    return;
                }
                auto next = completed;
                for (const auto id : ids)
                {
                    if (const auto found = locations.find(id); found != locations.end())
                    {
                        next.insert(found->second);
                    }
                }
                if (next == completed)
                {
                    return;
                }
                // Never publish an uncommitted check or retirement membership.
                commit(checkpoint(next));
                completed = std::move(next);
                publish();
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
                std::set<int64_t> batch;
                bool failed{};
                {
                    std::unique_lock lock{mutex};
                    changed.wait(lock,
                                 []
                                 {
                                     return stopping || overflow || !pending.empty();
                                 });
                    if (pending.empty() && !overflow && stopping)
                    {
                        break;
                    }
                    batch.swap(pending);
                    failed = overflow;
                    overflow = false;
                }
                try
                {
                    if (failed)
                    {
                        state.fail("queue_capacity_exceeded");
                    }
                    else
                    {
                        state.apply(batch);
                    }
                }
                catch (const std::exception& e)
                {
                    state.fail(e.what());
                }
            }
        }
    } // namespace
    auto location_id(std::wstring_view token, uint32_t fname_number) -> std::optional<int64_t>
    {
        constexpr std::wstring_view prefix = L"ITEM_AP_I";
        // Unreal stores a trailing decimal FName component as Number = suffix
        // + 1. The low-word lookup returns only the unnumbered base name.
        if (token.starts_with(prefix) && token.size() == prefix.size() + item_token_id_digits &&
            fname_number > 1)
        {
            return location_id(std::wstring(token) + L"_" + std::to_wstring(fname_number - 1));
        }
        if (!token.starts_with(prefix) ||
            token.size() <= prefix.size() + item_token_suffix_offset ||
            token[prefix.size() + item_token_id_digits] != L'_')
        {
            return {};
        }
        int category{}, index{};
        for (size_t i = 0; i < item_token_id_digits; ++i)
        {
            const auto ch = token[prefix.size() + i];
            if (ch < L'0' || ch > L'9')
            {
                return {};
            }
            if (i < item_token_category_digits)
            {
                category = category * 10 + ch - L'0';
            }
            else
            {
                index = index * 10 + ch - L'0';
            }
        }
        if (category < 1 || category > max_lucky_mark_category || index < 1 ||
            index > max_lucky_mark_index)
        {
            return {};
        }
        int64_t id{};
        for (auto ch : token.substr(prefix.size() + item_token_suffix_offset))
        {
            if (ch < L'0' || ch > L'9' || id > (kh3ap::json::max_safe_integer - (ch - L'0')) / 10)
            {
                return {};
            }
            id = id * 10 + ch - L'0';
        }
        return id > 0 ? std::optional<int64_t>(id) : std::nullopt;
    }
    auto start(const std::filesystem::path& directory) -> void
    {
        std::lock_guard lock{mutex};
        if (running)
        {
            return;
        }
        root = directory;
        pending.clear();
        stopping = overflow = false;
        running = true;
        snapshot.store(std::make_shared<const View>());
        snapshot_revision.fetch_add(1, std::memory_order_release);
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
    auto record(const std::filesystem::path& directory, std::wstring_view token, int32_t result,
                uint32_t fname_number) -> void
    {
        const auto id = location_id(token, fname_number);
        if (!id || result <= 0)
        {
            return;
        }
        std::lock_guard lock{mutex};
        if (!running || stopping || directory != root)
        {
            return;
        }
        if (pending.size() >= lucky_mark_pending_capacity && !pending.contains(*id))
        {
            overflow = true;
        }
        else
        {
            pending.insert(*id);
        }
        changed.notify_one();
    }
    auto view() -> std::shared_ptr<const View>
    {
        return snapshot.load(std::memory_order_acquire);
    }
    auto revision() -> uint64_t
    {
        return snapshot_revision.load(std::memory_order_acquire);
    }
} // namespace kh3ap::lucky_marks
