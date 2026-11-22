#pragma once
#include "kh02_file_cache.h"
#include <cstdint>
#include <mutex>
#include <new>
#include <optional>
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
            // Localization archives seek between their string table and namespace records.
            // Share their contents across those small reads using the existing header cache.
            constexpr size_t max_localization_file = 128 * 1024;
            if (length && ((file_size <= header_cache_max_file && suffix.ends_with(L".uasset")) ||
                           (file_size <= max_localization_file && suffix.ends_with(L".locres"))))
            {
                std::unique_lock lock(read_cache_mutex);
                return header_cache.read_with_unlocked_load(
                    file, static_cast<size_t>(file_size), offset, {static_cast<std::byte*>(destination), length}, lock,
                    [&load, file](std::span<std::byte> bytes)
                    { return load(file, 0, static_cast<uint32_t>(bytes.size()), bytes.data()); });
            }
            if (length && suffix.ends_with(L".uexp"))
            {
                std::unique_lock lock(read_cache_mutex);
                if (export_cache_enabled &&
                    kh02asset::cache_export_read(suffix, file_size, offset, length, export_cache.contains(file)))
                {
                    const auto read = export_cache.read_with_unlocked_load(
                        file, static_cast<size_t>(file_size - 4), offset,
                        {static_cast<std::byte*>(destination), length}, lock, [&load, file](std::span<std::byte> bytes)
                        { return load(file, 0, static_cast<uint32_t>(bytes.size()), bytes.data()); });
                    if (read && scenery_transfer && scenery_transfer->file == file)
                    {
                        // UE can request the last object first. Use one payload's worth of demand
                        // reads as the priority window, regardless of serialization order.
                        auto& remaining = scenery_transfer->remaining;
                        remaining -= std::min<uint64_t>(remaining, length);
                        if (!remaining)
                        {
                            scenery_transfer.reset();
                            export_cache.erase(file);
                        }
                    }
                    else if (read && file_size > export_cache_max_file && offset + length == file_size - 4)
                    {
                        // Large exports only stay in the cache through their initial transfer.
                        export_cache.erase(file);
                    }
                    return read;
                }
            }
            if (kh02asset::cache_range_read(suffix, file_size, offset, length))
            {
                std::unique_lock lock(read_cache_mutex);
                if (range_cache_enabled)
                {
                    const ReadRange request{file, offset, length};
                    const auto range = suffix.ends_with(L".uexp") ? export_read_window(request, file_size) : request;
                    return range_cache.read_with_unlocked_load(
                        range, range.length, offset - range.offset, {static_cast<std::byte*>(destination), length},
                        lock, [&load, range](std::span<std::byte> bytes)
                        { return load(range.file, range.offset, range.length, bytes.data()); });
                }
            }
            return load(file, offset, length, destination);
        }

        void cache_prepared_exports(uint32_t file, std::vector<uint8_t> bytes, std::wstring_view suffix = {})
        {
            if (bytes.size() <= 4 || bytes.capacity() > export_cache_budget)
            {
                return;
            }
            const bool large = bytes.size() > export_cache_max_file;
            bytes.resize(bytes.size() - 4);
            std::lock_guard lock(read_cache_mutex);
            if (export_cache_enabled)
            {
                try
                {
                    const bool scenery = suffix.ends_with(L"/dw_02_crystalground.uexp") ||
                                         suffix.ends_with(L"/dw_02_env_builtdata.uexp");
                    // Give these prepared meshes priority over speculative fills until UE consumes
                    // them. Ordinary demand reads still evict entries under the same byte limit.
                    const auto capacity = bytes.capacity();
                    const auto payload_size = bytes.size();
                    const bool compatible = scenery || !scenery_transfer ||
                                            !export_cache.keep_recent(scenery_transfer->file) ||
                                            capacity <= export_cache_budget - scenery_transfer->capacity;
                    export_cache.store_prepared(file, std::move(bytes), large && compatible);
                    if (scenery && export_cache.contains(file))
                    {
                        scenery_transfer = SceneryTransfer{file, capacity, payload_size};
                    }
                }
                catch (const std::bad_alloc&)
                {
                    // This cache fill is optional. The engine can still read the exports on demand.
                }
            }
        }

        void clear()
        {
            std::lock_guard lock(read_cache_mutex);
            header_cache.clear();
            export_cache.clear();
            scenery_transfer.reset();
            range_cache.clear();
        }

        void configure_exports(bool enabled)
        {
            std::lock_guard lock(read_cache_mutex);
            export_cache.clear();
            scenery_transfer.reset();
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

        struct SceneryTransfer
        {
            uint32_t file;
            size_t capacity;
            uint64_t remaining;
        };

        std::optional<SceneryTransfer> scenery_transfer;
        bool export_cache_enabled{true};
        bool range_cache_enabled{true};
    };
} // namespace kh02asset
