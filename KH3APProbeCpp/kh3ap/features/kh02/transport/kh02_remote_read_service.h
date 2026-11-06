#pragma once
#include "kh02_file_cache.h"
#include <cstdint>
#include <mutex>
#include <string_view>
#include <tuple>

namespace kh02asset
{
    // Coordinates cache choice and settings; loading always releases the cache mutex.
    class RemoteReadService
    {
      public:
        template <class Loader>
        auto read(uint32_t file, uint64_t file_size, std::wstring_view suffix, uint64_t offset, uint32_t length,
                  void* destination, Loader&& load) -> bool
        {
            if (length && file_size <= kh02asset::header_cache_max_file && suffix.ends_with(L".uasset"))
            {
                std::unique_lock lock(read_cache_mutex);
                return header_cache.read_with_unlocked_load(
                    file, static_cast<size_t>(file_size), offset, {static_cast<std::byte*>(destination), length}, lock,
                    [&load, file](std::span<std::byte> bytes)
                    { return load(file, 0, static_cast<uint32_t>(bytes.size()), bytes.data()); });
            }
            if (length && file_size <= kh02asset::export_cache_max_file && suffix.ends_with(L".uexp"))
            {
                std::unique_lock lock(read_cache_mutex);
                if (export_cache_enabled &&
                    kh02asset::cache_export_read(suffix, file_size, offset, length, export_cache.contains(file)))
                {
                    return export_cache.read_with_unlocked_load(
                        file, static_cast<size_t>(file_size - 4), offset,
                        {static_cast<std::byte*>(destination), length}, lock, [&load, file](std::span<std::byte> bytes)
                        { return load(file, 0, static_cast<uint32_t>(bytes.size()), bytes.data()); });
                }
            }
            if (kh02asset::cache_range_read(suffix, file_size, offset, length))
            {
                std::unique_lock lock(read_cache_mutex);
                if (range_cache_enabled)
                {
                    const kh02asset::ReadRange range{file, offset, length};
                    return range_cache.read_with_unlocked_load(range, length, 0,
                                                               {static_cast<std::byte*>(destination), length}, lock,
                                                               [&load, file, offset, length](std::span<std::byte> bytes)
                                                               { return load(file, offset, length, bytes.data()); });
                }
            }
            return load(file, offset, length, destination);
        }

        void clear()
        {
            std::lock_guard lock(read_cache_mutex);
            header_cache.clear();
            export_cache.clear();
            range_cache.clear();
        }

        void configure_exports(bool enabled)
        {
            std::lock_guard lock(read_cache_mutex);
            export_cache.clear();
            export_cache_enabled = enabled;
        }

        void configure_ranges(bool enabled)
        {
            std::lock_guard lock(read_cache_mutex);
            range_cache.clear();
            range_cache_enabled = enabled;
        }

        auto snapshot() const
        {
            std::lock_guard lock(read_cache_mutex);
            return std::tuple{header_cache.stats(), export_cache.stats(), export_cache_enabled, range_cache.stats(),
                              range_cache_enabled};
        }

      private:
        mutable std::mutex read_cache_mutex;
        FileReadCache header_cache{header_cache_budget};
        FileReadCache export_cache{export_cache_budget};
        RangeReadCache range_cache{range_cache_budget, range_cache_max_entries};
        bool export_cache_enabled{true};
        bool range_cache_enabled{true};
    };
} // namespace kh02asset
