#include "kh3ap/bridge/checks/level_checks.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#include "kh3ap/bridge/serialization/json.h"
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>

namespace kh3ap::level_checks
{
    namespace
    {
        constexpr auto maximum_character_level = 99;
        constexpr auto first_level_up_check = 2;
        constexpr auto max_exact_json_integer = 9007199254740991.0;

        using Json = kh3ap::json::Value;
        std::mutex mutex;
        std::condition_variable changed;
        std::thread worker;
        std::filesystem::path root;
        bool running{}, stopping{};
        uint64_t pending{};

        auto field(const Json& j, std::string_view key) -> const Json&
        { static const Json nil; return j.contains(key) ? j[key] : nil; }
        auto text(const Json& j) -> std::string
        { return j.is_string() ? j.get_ref<const std::string&>() : ""; }
        auto integer(const Json& j) -> int64_t
        {
            if (!j.is_number()) throw std::runtime_error("invalid_integer");
            const auto n = j.get<double>();
            if (!std::isfinite(n) || std::floor(n) != n || std::abs(n) > max_exact_json_integer)
                throw std::runtime_error("invalid_integer");
            return static_cast<int64_t>(n);
        }
        auto array(const Json& j) -> const Json::array_t&
        { if (!j.is_array()) throw std::runtime_error("invalid_array"); return j.get_ref<const Json::array_t&>(); }
        auto encode(const Json& j) -> std::string
        { std::string out; kh3ap::json::write(j, out); return out; }
        auto read_disk(const std::filesystem::path& path) -> std::optional<Json>
        {
            if (!std::filesystem::exists(path)) return {};
            if (std::filesystem::file_size(path) > json::limits::checkpoint_bytes) throw std::runtime_error("json_size_limit");
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("checkpoint_read_failed");
            const auto data = read_stream_bounded(input, json::limits::checkpoint_bytes);
            Json j;
            if (!data || !kh3ap::json::parse(j, *data)) throw std::runtime_error("invalid_json");
            return j;
        }
        auto commit(const Json& j) -> void
        {
            const auto data = encode(j);
            const auto destination = root / journal_file;
            const auto temporary = std::filesystem::path(destination.wstring() + L".tmp");
            HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
            if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("checkpoint_open_failed");
            DWORD written{};
            const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                written == data.size() && FlushFileBuffers(file);
            CloseHandle(file);
            if (!ok || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("checkpoint_commit_failed");
        }
        class State
        {
            std::string seed, error;
            int64_t slot{}, highest{1};
            std::set<int64_t> selected;

            auto checkpoint(int64_t level) const -> Json
            {
                Json::array_t ids;
                for (auto threshold : selected)
                    if (threshold <= level) ids.emplace_back(std::to_string(threshold));
                const auto count = ids.size();
                return Json{{"schema", "kh3ap.level_up_progress.v1"}, {"owner", "dll"},
                    {"seed_name", seed}, {"slot", slot}, {"highest_level", level},
                    {"record_count", count}, {"completed", std::move(ids)}, {"complete", true}};
            }
        public:
            auto publish() const -> void
            {
                auto j = checkpoint(highest);
                j["process_id"] = GetCurrentProcessId();
                j["ready"] = error.empty() && !seed.empty() && slot > 0;
                j["reason"] = error;
                pipe::write(root / state_file, encode(j));
            }
            auto recover() -> void
            {
                const auto metadata = json::read_seed_json(root / L"slot_data.json");
                if (!metadata) throw std::runtime_error("seed_metadata_missing");
                const auto& identity = field(*metadata, "kh3_seed");
                seed = text(field(identity, "seed_name")); slot = integer(field(identity, "slot"));
                if (seed.empty() || slot <= 0) throw std::runtime_error("seed_metadata_invalid");
                std::set<int64_t> ap_ids;
                for (const auto& row : array(field(field(*metadata, "bridge"), "locations")))
                {
                    if (text(field(row, "source_table")) != "LevelUp") continue;
                    const auto id = text(field(row, "source_id"));
                    int64_t threshold{};
                    for (auto ch : id)
                    {
                        if (ch < '0' || ch > '9' || threshold > maximum_character_level) throw std::runtime_error("invalid_level_threshold");
                        threshold = threshold * 10 + ch - '0';
                    }
                    if (threshold < first_level_up_check || threshold > maximum_character_level || std::to_string(threshold) != id ||
                        text(field(row, "bridge_key")) != "LevelUp:" + id)
                        throw std::runtime_error("invalid_level_threshold");
                    const auto ap_id = integer(field(row, "ap_location_id"));
                    if (ap_id <= 0 || !selected.insert(threshold).second || !ap_ids.insert(ap_id).second)
                        throw std::runtime_error("duplicate_or_invalid_location");
                }
                const auto saved = read_disk(root / journal_file);
                if (!saved) return;
                if (text(field(*saved, "schema")) != "kh3ap.level_up_progress.v1" ||
                    text(field(*saved, "owner")) != "dll" || text(field(*saved, "seed_name")) != seed ||
                    integer(field(*saved, "slot")) != slot || !field(*saved, "complete").is_boolean() ||
                    !field(*saved, "complete").get<bool>())
                    throw std::runtime_error("checkpoint_identity_mismatch");
                const auto level = integer(field(*saved, "highest_level"));
                if (level < 1 || level > maximum_character_level) throw std::runtime_error("invalid_checkpoint_level");
                // Complete membership must exactly match the selected thresholds
                // at this high-water level, including sparse seed selections.
                const auto expected = checkpoint(level);
                if (encode(field(*saved, "completed")) != encode(field(expected, "completed")) ||
                    integer(field(*saved, "record_count")) != integer(field(expected, "record_count")))
                    throw std::runtime_error("invalid_checkpoint_membership");
                highest = level;
            }
            auto fail(std::string reason) -> void { error = std::move(reason); publish(); }
            auto apply(uint64_t level) -> void
            {
                if (!error.empty() || level <= static_cast<uint64_t>(highest)) return;
                // Never expose achievements from an uncommitted write.
                commit(checkpoint(static_cast<int64_t>(level)));
                highest = static_cast<int64_t>(level); publish();
            }
        };
        auto run() -> void
        {
            State state;
            try { state.recover(); state.publish(); }
            catch (const std::exception& e) { state.fail(e.what()); }
            for (;;)
            {
                uint64_t level{};
                {
                    std::unique_lock lock{mutex};
                    changed.wait(lock, [] { return stopping || pending != 0; });
                    if (pending == 0 && stopping) break;
                    level = pending; pending = 0;
                }
                try { state.apply(level); }
                catch (const std::exception& e) { state.fail(e.what()); }
            }
        }
    }
    auto start(const std::filesystem::path& directory) -> void
    {
        std::lock_guard lock{mutex}; if (running) return;
        root = directory; pending = 0; stopping = false; running = true;
        worker = std::thread(run);
    }
    auto stop() -> void
    {
        { std::lock_guard lock{mutex}; if (!running) return; stopping = true; changed.notify_one(); }
        worker.join();
        std::lock_guard lock{mutex}; running = false;
    }
    auto observe(const std::filesystem::path& directory, uint64_t level) -> void
    {
        if (level < 1 || level > maximum_character_level) return;
        std::lock_guard lock{mutex}; if (!running || stopping || directory != root) return;
        pending = std::max(pending, level); changed.notify_one();
    }
}
