#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    inline constexpr auto music_chest_status_retry_interval = std::chrono::seconds(5);

    // Cache only successful, verified publications. This is diagnostic status,
    // never the durable record of a chest opening.
    class RuntimeMusicChestStatus
    {
    public:
        using Clock = std::chrono::steady_clock;

        void reset()
        {
            path_.reset();
            text_.clear();
            next_file_check_ = {};
        }

        template <typename Exists, typename Write>
        bool publish(const std::filesystem::path& path, std::string_view text,
                     Clock::time_point now, Exists exists, Write write)
        {
            if (path_ && *path_ == path && text_ == text)
            {
                if (now < next_file_check_)
                {
                    return true;
                }
                // Recover a deleted sidecar without reading/writing it every tick.
                if (exists(path))
                {
                    next_file_check_ = now + music_chest_status_retry_interval;
                    return true;
                }
            }
            if (!write(path, text))
            {
                reset(); // A failed write must retry even if the payload is unchanged.
                return false;
            }
            path_ = path;
            text_ = text;
            next_file_check_ = now + music_chest_status_retry_interval;
            return true;
        }

    private:
        std::optional<std::filesystem::path> path_;
        std::string text_;
        Clock::time_point next_file_check_{};
    };
} // namespace kh3ap
