#include "kh02_package_adapter.h"
#include "kh02_package_format.h"
#include "kh02_index_state.h"
#include "kh02_asset_protocol.h"
#include <list>
#include <atomic>
#include <map>
#include <mutex>

namespace kh02asset
{
    namespace
    {
        constexpr std::size_t cache_byte_limit = 32 * 1024 * 1024;
        constexpr std::size_t cache_entry_limit = 4096;
        constexpr uint32_t missing_file = std::numeric_limits<uint32_t>::max();

        struct Edit
        {
            uint32_t offset;
            uint8_t value;
        };

        struct Prepared
        {
            uint32_t header{}, exports = missing_file;
            std::vector<Edit> header_edits, export_edits;
            bool valid{};
            std::string error;

            auto bytes() const -> std::size_t
            {
                return sizeof(Prepared) + error.capacity() +
                       sizeof(Edit) * (header_edits.capacity() + export_edits.capacity());
            }
        };

        auto edits(std::span<const uint8_t> original, std::span<const uint8_t> changed)
            -> std::vector<Edit>
        {
            package::require(original.size() == changed.size(), "kh02_adapter_size_changed");
            std::size_t count{};
            for (std::size_t i = 0; i < original.size(); ++i)
            {
                count += original[i] != changed[i];
            }
            package::require(count <= cache_byte_limit / sizeof(Edit), "kh02_adapter_edit_limit");
            std::vector<Edit> result;
            result.reserve(count);
            for (std::size_t i = 0; i < original.size(); ++i)
            {
                if (original[i] != changed[i])
                {
                    result.push_back({static_cast<uint32_t>(i), changed[i]});
                }
            }
            return result;
        }

        auto read_all(uint32_t id, const PackageAdapter::RawRead& raw, std::size_t maximum) -> Bytes
        {
            const auto size = asset_index().files.at(id).size;
            package::require(size <= maximum, "kh02_adapter_preparation_limit");
            Bytes result(static_cast<std::size_t>(size));
            for (std::size_t offset = 0; offset < result.size();)
            {
                const auto length =
                    static_cast<uint32_t>(std::min<std::size_t>(max_read, result.size() - offset));
                package::require(raw(id, offset, length, result.data() + offset),
                                 "kh02_adapter_source_read");
                offset += length;
            }
            return result;
        }
    } // namespace

    struct PackageAdapter::Impl
    {
        std::mutex mutex;
        mutable std::mutex error_mutex;
        std::string error;
        const AssetIndex* index{};
        std::map<std::wstring, uint32_t> paths;
        std::list<uint32_t> recent;
        struct Entry
        {
            std::shared_ptr<const Prepared> value;
            std::list<uint32_t>::iterator age;
        };
        std::map<uint32_t, Entry> entries;
        std::size_t cached_bytes{};
        std::atomic<uint64_t> hits{}, preparations{}, evictions{}, entry_count{}, byte_count{};

        auto prepare(uint32_t file, const RawRead& raw) -> std::shared_ptr<const Prepared>
        {
            // Serialize preparation to bound temporary package buffers. Raw
            // reads take only the pipe lock, never this cache's lock.
            std::lock_guard lock(mutex);
            const auto& current = asset_index();
            if (!index)
            {
                for (uint32_t i = 0; i < current.files.size(); ++i)
                {
                    auto path = current.files[i].source;
                    std::transform(path.begin(), path.end(), path.begin(),
                                   [](wchar_t value)
                                   {
                                       return value >= L'A' && value <= L'Z' ? value + (L'a' - L'A')
                                                                             : value;
                                   });
                    paths.emplace(std::move(path), i);
                }
                index = &current;
            }
            package::require(index == &current, "kh02_adapter_index_changed");
            const auto& source = current.files.at(file).source;
            const auto extension = source.find_last_of('.');
            package::require(extension != std::wstring::npos, "kh02_adapter_extension");
            auto base = source.substr(0, extension);
            std::transform(base.begin(), base.end(), base.begin(),
                           [](wchar_t value)
                           {
                               return value >= L'A' && value <= L'Z' ? value + (L'a' - L'A')
                                                                     : value;
                           });
            auto header = paths.find(base + L".uasset");
            if (header == paths.end())
            {
                header = paths.find(base + L".umap");
            }
            package::require(header != paths.end(), "kh02_adapter_header_missing");
            if (const auto found = entries.find(header->second); found != entries.end())
            {
                ++hits;
                recent.splice(recent.begin(), recent, found->second.age);
                return found->second.value;
            }
            auto prepared = std::make_shared<Prepared>();
            ++preparations;
            prepared->header = header->second;
            const auto payload = paths.find(base + L".uexp");
            if (payload != paths.end())
            {
                prepared->exports = payload->second;
            }
            // Failed reads are not cached: transport reconnection may recover.
            const auto original_header =
                read_all(prepared->header, raw, package::maximum_header_bytes);
            const auto original_exports =
                payload == paths.end()
                    ? Bytes{}
                    : read_all(prepared->exports, raw, package::maximum_export_bytes);
            try
            {
                const auto& header_source = current.files[prepared->header].source;
                const auto identity = source_ascii(header_source);
                const auto name =
                    identity.substr(source_content_prefix.size(),
                                    identity.find_last_of('.') - source_content_prefix.size());
                const auto changed = adapt_package(name, original_header, original_exports);
                prepared->header_edits = edits(original_header, changed.header);
                prepared->export_edits = edits(original_exports, changed.exports);
                prepared->valid = true;
            }
            catch (const std::exception& failure)
            {
                // Remember an unsupported package without retaining its bytes.
                // The caller rejects reads rather than passing incompatible data.
                prepared->header_edits = std::vector<Edit>{};
                prepared->export_edits = std::vector<Edit>{};
                prepared->error = std::string(failure.what()).substr(0, 256);
            }
            const auto size = prepared->bytes();
            package::require(size <= cache_byte_limit, "kh02_adapter_cache_entry_limit");
            while (cached_bytes + size > cache_byte_limit || entries.size() >= cache_entry_limit)
            {
                auto candidate = recent.end();
                for (auto item = recent.end(); item != recent.begin();)
                {
                    --item;
                    if (entries.at(*item).value.use_count() == 1)
                    {
                        candidate = item;
                        break;
                    }
                }
                package::require(candidate != recent.end(), "kh02_adapter_cache_busy");
                const auto old = entries.find(*candidate);
                cached_bytes -= old->second.value->bytes();
                entries.erase(old);
                recent.erase(candidate);
                ++evictions;
            }
            recent.push_front(prepared->header);
            cached_bytes += size;
            entries.emplace(prepared->header, Entry{prepared, recent.begin()});
            entry_count = entries.size();
            byte_count = cached_bytes;
            return prepared;
        }
    };

    PackageAdapter::PackageAdapter() : impl(std::make_unique<Impl>())
    {
    }
    PackageAdapter::~PackageAdapter() = default;

    void PackageAdapter::clear()
    {
        std::lock_guard lock(impl->mutex);
        impl->entries.clear();
        impl->recent.clear();
        impl->paths.clear();
        impl->index = nullptr;
        impl->cached_bytes = 0;
        impl->hits = 0;
        impl->preparations = 0;
        impl->evictions = 0;
        impl->entry_count = 0;
        impl->byte_count = 0;
        std::lock_guard error_lock(impl->error_mutex);
        impl->error.clear();
    }

    auto PackageAdapter::read(uint32_t file, uint64_t offset, uint32_t length, void* output,
                              const RawRead& raw) -> bool
    {
        try
        {
            const auto& index = asset_index();
            if (file >= index.files.size() || offset > index.files[file].size ||
                length > index.files[file].size - offset || (length && !output))
            {
                return false;
            }
            const auto& path = index.files[file].suffix;
            if (!length || !(path.ends_with(L".uasset") || path.ends_with(L".umap") ||
                             path.ends_with(L".uexp")))
            {
                return raw(file, offset, length, output);
            }
            const auto prepared = impl->prepare(file, raw);
            if (!prepared->valid)
            {
                throw std::runtime_error(prepared->error);
            }
            package::require(raw(file, offset, length, output), "kh02_adapter_raw_read_failed");
            const auto& changes =
                file == prepared->header ? prepared->header_edits : prepared->export_edits;
            const auto first = std::lower_bound(changes.begin(), changes.end(), offset,
                                                [](const Edit& edit, uint64_t at)
                                                {
                                                    return edit.offset < at;
                                                });
            for (auto edit = first; edit != changes.end() && edit->offset - offset < length; ++edit)
            {
                static_cast<uint8_t*>(output)[edit->offset - offset] = edit->value;
            }
            return true;
        }
        catch (const std::exception& failure)
        {
            std::lock_guard lock(impl->error_mutex);
            impl->error =
                "file=" + std::to_string(file) + ": " + std::string(failure.what()).substr(0, 256);
            return false;
        }
    }

    auto PackageAdapter::last_error() const -> std::string
    {
        std::lock_guard lock(impl->error_mutex);
        return impl->error;
    }

    auto PackageAdapter::snapshot() const -> Stats
    {
        return {impl->hits.load(), impl->preparations.load(), impl->evictions.load(),
                impl->entry_count.load(), impl->byte_count.load()};
    }
} // namespace kh02asset
