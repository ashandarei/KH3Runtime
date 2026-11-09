#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <utility>

namespace kh3ap
{
    // Game-thread consumer, diagnostic requests only. No UObject or gameplay
    // context crosses into the worker. One changed file starts at most one read;
    // the existing pending-command wake handles completion and safety retries.
    class ManualGrantFileReader
    {
        struct Snapshot
        {
            std::filesystem::path path;
            std::filesystem::file_time_type write_time;
            uintmax_t size;
            auto operator==(const Snapshot&) const -> bool = default;
        };

        static auto snapshot(const std::filesystem::path& path) -> std::optional<Snapshot>
        {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error) return std::nullopt;
            const auto write_time = std::filesystem::last_write_time(path, error);
            if (error) return std::nullopt;
            const auto size = std::filesystem::file_size(path, error);
            if (error) return std::nullopt;
            return Snapshot{path, write_time, size};
        }

        std::optional<Snapshot> selected_;
        std::optional<std::string> text_;
        std::future<std::optional<std::string>> pending_;

    public:
        using Read = std::function<std::optional<std::string>(const std::filesystem::path&)>;

        auto ready() const -> bool
        {
            return pending_.valid() &&
                   pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        // The returned text remains owned here until the next poll. A missing,
        // failed or unfinished read cannot dispatch a grant. Failed reads remain
        // dormant until the request changes instead of retrying disk I/O.
        auto poll(const std::filesystem::path& path, Read read) -> const std::string*
        {
            const auto current = snapshot(path);
            if (pending_.valid())
            {
                if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                    return nullptr;
                try { text_ = pending_.get(); }
                catch (...) { text_.reset(); }
                if (current != selected_)
                {
                    selected_.reset();
                    text_.reset();
                }
            }
            if (!current)
            {
                selected_.reset();
                text_.reset();
                return nullptr;
            }
            if (current != selected_)
            {
                selected_ = current;
                text_.reset();
                try
                {
                    pending_ = std::async(std::launch::async, [expected = *current, read = std::move(read)] {
                        if (snapshot(expected.path) != expected) return std::optional<std::string>{};
                        auto text = read(expected.path);
                        return snapshot(expected.path) == expected ? text : std::nullopt;
                    });
                }
                catch (...) { /* Fail closed; never fall back to a blocking game-thread read. */ }
                return nullptr;
            }
            return text_ ? &*text_ : nullptr;
        }
    };
}
