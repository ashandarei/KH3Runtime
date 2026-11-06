#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    // Kernel notifications wake the observer; only changes to named request
    // files dispatch work. Ordinary status/log writes leave handlers idle.
    class RequestFileWatcher
    {
        struct Signature
        {
            std::filesystem::file_time_type write{};
            uintmax_t size{};
            bool exists{};
            auto operator==(const Signature&) const -> bool = default;
        };
        struct Entry
        {
            std::wstring name;
            Signature signature;
        };
        HANDLE changes_{INVALID_HANDLE_VALUE};
        std::optional<std::filesystem::path> directory_;
        std::vector<Entry> entries_;
        bool pending_{};

        void close_notification() noexcept
        {
            if (changes_ != INVALID_HANDLE_VALUE)
            {
                FindCloseChangeNotification(changes_);
                changes_ = INVALID_HANDLE_VALUE;
            }
        }
        bool open_notification()
        {
            changes_ = FindFirstChangeNotificationW(directory_->c_str(), FALSE,
                                                    FILE_NOTIFY_CHANGE_FILE_NAME |
                                                        FILE_NOTIFY_CHANGE_LAST_WRITE |
                                                        FILE_NOTIFY_CHANGE_SIZE);
            return changes_ != INVALID_HANDLE_VALUE;
        }
        static Signature signature(const std::filesystem::path& path)
        {
            Signature result{};
            std::error_code error;
            result.exists = std::filesystem::is_regular_file(path, error);
            if (result.exists)
            {
                result.write = std::filesystem::last_write_time(path, error);
                result.size = std::filesystem::file_size(path, error);
            }
            return result;
        }

    public:
        RequestFileWatcher(std::initializer_list<std::wstring_view> names)
        {
            for (auto name : names)
            {
                entries_.push_back({std::wstring{name}, {}});
            }
        }
        RequestFileWatcher(const RequestFileWatcher&) = delete;
        RequestFileWatcher& operator=(const RequestFileWatcher&) = delete;
        ~RequestFileWatcher()
        {
            close_notification();
        }

        auto update(const std::optional<std::filesystem::path>& root, bool retry_pending = false)
            -> bool
        {
            bool notified = false;
            if (root != directory_)
            {
                close_notification();
                directory_ = root;
                for (auto& entry : entries_)
                {
                    entry.signature = {};
                }
                pending_ = false;
                notified = true;
            }
            // A client can create the selected bridge directory after startup.
            // Retry only the unfinished subscription; once connected, idle work
            // is a cheap event check with no recurring filesystem reads.
            if (directory_ && changes_ == INVALID_HANDLE_VALUE)
            {
                notified = open_notification() || notified;
            }
            if (changes_ != INVALID_HANDLE_VALUE &&
                WaitForSingleObject(changes_, 0) == WAIT_OBJECT_0)
            {
                if (!FindNextChangeNotification(changes_))
                {
                    close_notification();
                }
                notified = true;
            }
            if (!root || (!notified && !(retry_pending && pending_)))
            {
                return false;
            }
            bool changed = false;
            pending_ = false;
            for (auto& entry : entries_)
            {
                const auto next = signature(*root / entry.name);
                changed = changed || entry.signature != next;
                entry.signature = next;
                pending_ = pending_ || next.exists;
            }
            return changed || (retry_pending && pending_);
        }
    };
} // namespace kh3ap
