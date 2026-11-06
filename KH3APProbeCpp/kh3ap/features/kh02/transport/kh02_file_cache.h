#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <limits>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kh02asset
{
    // Assets are immutable for the bound provider/catalog. Retain only validated,
    // relocated bytes in RAM; each caller controls admission and its byte budget.
    // The caller serializes access and clears this cache when binding a provider.
    struct ReadCacheStats
    {
        size_t bytes{};
        uint64_t hits{}, misses{}, evictions{}, reused_bytes{};
    };

    template <class Key, class Hash = std::hash<Key>> class BoundedReadCache
    {
      public:
        using Stats = ReadCacheStats;

        explicit BoundedReadCache(size_t budget, size_t max_entries = std::numeric_limits<size_t>::max())
            : budget_(budget), max_entries_(max_entries)
        {
        }

        BoundedReadCache(const BoundedReadCache&) = delete;
        auto operator=(const BoundedReadCache&) -> BoundedReadCache& = delete;

        bool contains(const Key& key) const
        {
            return entries_.contains(key);
        }

        template <class Load>
        bool read(const Key& key, size_t data_size, uint64_t offset, std::span<std::byte> output, Load load)
        {
            if (!max_entries_ || data_size > budget_ || offset > data_size || output.size() > data_size - offset)
            {
                return false;
            }

            auto found = entries_.find(key);
            if (found != entries_.end())
            {
                if (found->second.bytes.size() != data_size)
                {
                    return false;
                }
                ++stats_.hits;
                stats_.reused_bytes += output.size();
                recent_.splice(recent_.begin(), recent_, found->second.position);
            }
            else
            {
                ++stats_.misses;
                const auto generation = generation_;
                std::vector<std::byte> bytes(data_size);
                if (!load(std::span<std::byte>(bytes)) || generation != generation_)
                {
                    return false;
                }
                // The load may release the caller's mutex. A concurrent clear
                // invalidates it; another miss may already have filled this key.
                found = entries_.find(key);
                if (found != entries_.end())
                {
                    if (found->second.bytes.size() != data_size)
                    {
                        return false;
                    }
                    recent_.splice(recent_.begin(), recent_, found->second.position);
                }
                else
                {
                    // Failed reads never replace a valid cache entry or evict data.
                    while (stats_.bytes + data_size > budget_ || entries_.size() >= max_entries_)
                    {
                        const auto oldest = recent_.back();
                        stats_.bytes -= entries_.at(oldest).bytes.size();
                        entries_.erase(oldest);
                        recent_.pop_back();
                        ++stats_.evictions;
                    }
                    recent_.push_front(key);
                    found = entries_.emplace(key, Entry{std::move(bytes), recent_.begin()}).first;
                    stats_.bytes += data_size;
                }
            }

            if (!output.empty())
            {
                std::copy_n(found->second.bytes.data() + offset, output.size(), output.data());
            }
            return true;
        }

        template <class Load>
        bool read_with_unlocked_load(const Key& key, size_t data_size, uint64_t offset, std::span<std::byte> output,
                                     std::unique_lock<std::mutex>& lock, Load load)
        {
            if (!lock.owns_lock())
            {
                return false;
            }
            return read(key, data_size, offset, output,
                        [&](std::span<std::byte> bytes)
                        {
                            lock.unlock();
                            bool loaded{};
                            try
                            {
                                loaded = load(bytes);
                            }
                            catch (...)
                            {
                                lock.lock();
                                throw;
                            }
                            lock.lock();
                            return loaded;
                        });
        }

        void clear()
        {
            ++generation_;
            entries_.clear();
            recent_.clear();
            stats_ = {};
        }

        auto stats() const -> Stats
        {
            return stats_;
        }

      private:
        struct Entry
        {
            std::vector<std::byte> bytes;
            typename std::list<Key>::iterator position;
        };

        size_t budget_;
        size_t max_entries_;
        Stats stats_;
        uint64_t generation_{};
        std::list<Key> recent_;
        std::unordered_map<Key, Entry, Hash> entries_;
    };

    using FileReadCache = BoundedReadCache<uint32_t>;

    struct ReadRange
    {
        uint32_t file;
        uint64_t offset;
        uint32_t length;
        bool operator==(const ReadRange&) const = default;
    };

    struct ReadRangeHash
    {
        size_t operator()(const ReadRange& range) const
        {
            auto hash = std::hash<uint64_t>{}(range.offset);
            for (const auto value : {range.file, range.length})
            {
                hash ^= std::hash<uint32_t>{}(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

    using RangeReadCache = BoundedReadCache<ReadRange, ReadRangeHash>;

    constexpr size_t header_cache_budget = 16 * 1024 * 1024;
    constexpr size_t header_cache_max_file = 64 * 1024;
    constexpr size_t export_cache_budget = 64 * 1024 * 1024;
    constexpr size_t export_cache_max_file = 1024 * 1024;
    constexpr size_t range_cache_budget = 64 * 1024 * 1024;
    constexpr size_t range_cache_max_read = 32 * 1024;
    constexpr size_t range_cache_max_header = 256 * 1024;
    constexpr size_t range_cache_max_entries = 16384;

    inline bool cache_range_read(std::wstring_view path, uint64_t size, uint64_t offset, uint32_t length)
    {
        if (!length || offset > size || length > size - offset)
        {
            return false;
        }
        // Small serialized ranges stay useful across map revisits. Large bulk
        // reads would evict them before reuse, so continue streaming those.
        if (length <= range_cache_max_read)
        {
            return true;
        }
        // A map's .umap is its package header, with exports in a separate file.
        // Retain complete headers exactly as requested, without read-ahead.
        return (path.ends_with(L".umap") || path.ends_with(L".uasset")) && size <= range_cache_max_header &&
               offset == 0 && length == size;
    }

    inline bool cache_export_read(std::wstring_view path, uint64_t size, uint64_t offset, uint32_t length,
                                  bool already_cached)
    {
        // UE4 normally requests the serialized exports without their final
        // four-byte package tag. Cache exactly that prefix, with no read-ahead.
        // Partial misses and reads of the footer still use the original reader.
        if (size <= 4 || size > export_cache_max_file || !path.ends_with(L".uexp") || !length)
        {
            return false;
        }
        const auto payload_size = size - 4;
        return offset <= payload_size && length <= payload_size - offset &&
               (already_cached || (offset == 0 && length == payload_size));
    }
} // namespace kh02asset
