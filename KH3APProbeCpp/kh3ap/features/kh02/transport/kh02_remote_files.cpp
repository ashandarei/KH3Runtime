#include "kh02_remote_files.h"
#include "kh02_file_adapter.h"
#include "kh02_async_reads.h"
#include "kh02_asset_protocol.h"
#include "kh02_index_state.h"
#include "kh02_remote_read_service.h"
#include "kh02_remote_pipe_transport.h"
#include "kh02_asset_path.h"
#include "kh3ap/features/kh02/core/kh02_voice_language.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh02_rpc_metrics.h"
#endif
#include <Unreal/FMemory.hpp>
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <set>
#include <tuple>

extern "C" void* kh02_make_sync_handle(unsigned int file);
extern "C" void* kh02_make_async_handle(unsigned int file);
extern "C" unsigned int kh02_async_request_size();

extern "C" void* kh02_unreal_malloc(size_t count, unsigned int alignment)
{
    return RC::Unreal::FMemory::Malloc(count, alignment);
}

extern "C" void* kh02_unreal_realloc(void* data, size_t count, unsigned int alignment)
{
    return RC::Unreal::FMemory::Realloc(data, count, alignment);
}

extern "C" void kh02_unreal_free(void* data)
{
    RC::Unreal::FMemory::Free(data);
}

extern "C" size_t kh02_unreal_quantize(size_t count, unsigned int alignment)
{
    return RC::Unreal::FMemory::QuantizeSize(count, alignment);
}

namespace
{
    std::atomic<uint64_t> sync_opens{0}, async_opens{0}, exists_calls{0};

    auto remote_transport() -> kh3ap::Kh02RemotePipeTransport&
    {
        static kh3ap::Kh02RemotePipeTransport transport;
        return transport;
    }

    kh02asset::RemoteReadService read_service;
    std::atomic<uint64_t> timestamp_calls{0};
    std::atomic<uint64_t> size_requests{0}, read_requests{0}, callbacks{0}, callbacks_returned{0},
        missing_callbacks{0}, canceled_callbacks{0}, callback_failures{0}, last_size{0};

    auto native_adapter() -> kh3ap::Kh02NativeFileAdapter&
    {
        static kh3ap::Kh02NativeFileAdapter adapter;
        return adapter;
    }

    std::unordered_map<std::wstring, uint32_t, kh02asset::AssetPathHash, kh02asset::AssetPathEqual>
        file_ids;
    std::set<uint32_t> paired_voice_files;
    std::string bound_voice_language;
    thread_local bool benchmark_reconnect{};

    bool rpc(uint32_t file, uint64_t offset, uint32_t length, void* output)
    {
        return remote_transport().read(file, offset, length, output, benchmark_reconnect);
    }

    auto file_id(const wchar_t* path) -> int
    {
        if (!path || !remote_transport().provider_pid())
        {
            return -1;
        }
        const auto suffix = kh02asset::content_path_suffix(path);
        if (!suffix.empty())
        {
            const auto found = file_ids.find(suffix);
            if (found != file_ids.end())
            {
                return static_cast<int>(found->second);
            }
        }
        return -1;
    }

    bool file_exists(void* self, const wchar_t* path)
    {
        if (file_id(path) >= 0)
        {
            ++exists_calls;
            return true;
        }
        return native_adapter().forward_exists(self, path);
    }

    int64_t file_size(void* self, const wchar_t* path)
    {
        if (const auto id = file_id(path); id >= 0)
        {
            return kh02asset::asset_index().files[id].size;
        }
        return native_adapter().forward_size(self, path);
    }

    void* timestamp(void* self, int64_t* result, const wchar_t* path)
    {
        // UE4.17 FDateTime returns through a hidden result pointer. PackageName's
        // existence test uses GetTimeStamp rather than FileExists in this build.
        if (file_id(path) >= 0)
        {
            ++timestamp_calls;
            *result = 621355968000000000LL;
            return result;
        }
        return native_adapter().forward_timestamp(self, result, path);
    }

    void* stat_data(void* self, void* result, const wchar_t* path)
    {
        if (const auto id = file_id(path); id >= 0)
        {
            struct Stat
            {
                int64_t creation, access, modified, size;
                uint8_t flags;
                uint8_t padding[7];
            };

            static_assert(sizeof(Stat) == 40);
            const Stat value{621355968000000000LL,
                             621355968000000000LL,
                             621355968000000000LL,
                             static_cast<int64_t>(kh02asset::asset_index().files[id].size),
                             6,
                             {}};
            std::memcpy(result, &value, sizeof(value));
            return result;
        }
        return native_adapter().forward_stat(self, result, path);
    }

    void* open_read(void* self, const wchar_t* path, bool allow_write)
    {
        if (const auto id = file_id(path); id >= 0)
        {
            ++sync_opens;
            return kh02_make_sync_handle(id);
        }
        return native_adapter().forward_open(self, path, allow_write);
    }

    void* open_async(void* self, const wchar_t* path)
    {
        if (const auto id = file_id(path); id >= 0)
        {
            ++async_opens;
            return kh02_make_async_handle(id);
        }
        return native_adapter().forward_async(self, path);
    }

    void prepare_native_binding(uint32_t pid)
    {
        using namespace kh3ap;
        if (!kh02asset::index_ready() || remote_transport().provider_pid() != pid)
        {
            throw std::runtime_error("remote_provider_index_not_prepared");
        }
        const auto& assets = kh02asset::asset_index();
        const auto voice_language = current_kh02_voice_language();
        bound_voice_language = kh02_voice_language_code(voice_language);
        file_ids.clear();
        file_ids.reserve(assets.files.size());
        for (uint32_t i = 0; i < assets.files.size(); ++i)
        {
            if (!assets.files[i].suffix.empty())
            {
                file_ids.emplace(assets.files[i].suffix, i);
            }
        }
        for (const auto& pair : assets.voice_pairs)
        {
            const auto selected = pair[voice_language == Kh02VoiceLanguage::japanese ? 1 : 0];
            file_ids.at(assets.files[pair[0]].suffix) = selected;
            file_ids.at(assets.files[pair[1]].suffix) = selected;
            paired_voice_files.insert(pair[0]);
            paired_voice_files.insert(pair[1]);
        }
        read_service.clear();
        kh02_async_reads();
    }

    void rollback_native_binding()
    {
        remote_transport().disable();
    }

    // Initialized after the catalog, destroyed before it and the transport.
    // Restore file dispatch first, then drain callbacks before caches disappear.
    struct AsyncRuntime
    {
        kh02asset::AsyncReads reads;

        ~AsyncRuntime()
        {
            native_adapter().restore_dispatch();
            reads.shutdown();
        }
    };
} // namespace

extern "C" bool kh02_remote_read(unsigned int file, unsigned long long offset, unsigned int length,
                                 void* destination)
{
    const auto& assets = kh02asset::asset_index();
    if (!remote_transport().provider_pid() || file >= assets.files.size() ||
        (length && !destination))
    {
        return false;
    }
    const auto& asset = assets.files[file];
    return read_service.read(file, asset.size, asset.suffix, offset, length, destination, rpc);
}

extern "C" long long kh02_remote_file_size(unsigned int file)
{
    return kh02asset::asset_index().files.at(file).size;
}

extern "C" void kh02_async_event(unsigned int kind, unsigned long long first,
                                 unsigned long long second)
{
    if (kind == 0)
    {
        ++size_requests;
        last_size = second;
    }
    if (kind == 1)
    {
        ++read_requests;
    }
    if (kind == 2)
    {
        if (first)
        {
            ++callbacks;
        }
        else
        {
            ++missing_callbacks;
        }
        if (second)
        {
            ++canceled_callbacks;
        }
    }
    if (kind == 3)
    {
        ++callbacks_returned;
    }
    if (kind == 4)
    {
        ++callback_failures;
    }
}

namespace kh3ap
{
    void prepare_kh02_asset_index(uint32_t pid)
    {
        remote_transport().bind(pid);
        try
        {
            remote_transport().load_index();
            if (!rpc(0, 0, 0, nullptr))
            {
                throw std::runtime_error("remote_provider_preflight_failed");
            }
        }
        catch (...)
        {
            remote_transport().disable();
            throw;
        }
    }

    auto kh02_async_reads() -> kh02asset::AsyncReads&
    {
        // Register the catalog's destruction first, even if status is queried
        // before the provider is enabled.
        kh02asset::asset_index();
        remote_transport();
        native_adapter();
        static AsyncRuntime runtime;
        return runtime.reads;
    }

    auto configure_kh02_async_reads(bool enabled) -> std::string
    {
        if (!kh02_async_reads().configure(enabled))
        {
            throw std::runtime_error("Cannot change 0.2 async reads while requests are pending");
        }
        return kh02_remote_files_status();
    }

    auto configure_kh02_range_cache(bool enabled) -> std::string
    {
        read_service.configure_ranges(enabled);
        return kh02_remote_files_status();
    }

    auto kh02_remote_files_benchmark() -> std::string
    {
        if (!native_adapter().installed() || !remote_transport().provider_pid())
        {
            throw std::runtime_error("kh02_benchmark_provider_required");
        }
        const auto length =
            static_cast<uint32_t>(std::min<uint64_t>(kh02asset::asset_index().files[0].size, 4096));
        if (!length)
        {
            throw std::runtime_error("kh02_benchmark_empty_control");
        }
        std::vector<unsigned char> payload(length);
        uint64_t reference{};
        auto checksum = [&]
        {
            uint64_t hash = 14695981039346656037ull;
            for (auto c : payload)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        };

        struct RestoreMode
        {
            ~RestoreMode()
            {
                benchmark_reconnect = false;
            }
        } restore_mode;

        std::ostringstream out;
        out << "{\"requests_per_mode\":128,\"bytes_per_request\":" << length
            << ",\"disk_cache\":false,\"modes\":[";
        for (int mode = 0; mode < 2; ++mode)
        {
            benchmark_reconnect = mode == 0;
            const auto start = std::chrono::steady_clock::now();
            const auto previous_connections = remote_transport().connection_count();
            for (int i = 0; i < 128; ++i)
            {
                if (!rpc(0, 0, length, payload.data()))
                {
                    throw std::runtime_error("kh02_benchmark_read_failed");
                }
                const auto hash = checksum();
                if (!reference)
                {
                    reference = hash;
                }
                if (hash != reference)
                {
                    throw std::runtime_error("kh02_benchmark_checksum_mismatch");
                }
            }
            if (mode)
            {
                out << ',';
            }
            out << "{\"mode\":\"" << (mode ? "persistent_connection" : "connection_per_request")
                << "\",\"elapsed_us\":"
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count()
                << ",\"connections\":"
                << remote_transport().connection_count() - previous_connections << '}';
        }
        out << "],\"identical_payloads\":true}";
        return out.str();
    }

    auto configure_kh02_export_cache(bool enabled) -> std::string
    {
        read_service.configure_exports(enabled);
        return kh02_remote_files_status();
    }

    auto kh02_remote_files_status() -> std::string
    {
        const auto [headers, exports, exports_enabled, ranges, ranges_enabled] =
            read_service.snapshot();
        const auto transport_stats = remote_transport().snapshot();
        const auto async = kh02_async_reads().stats();
        std::ostringstream out;
        out << std::boolalpha << "{\"enabled\":" << native_adapter().installed()
            << ",\"provider_pid\":" << remote_transport().provider_pid()
            << ",\"requests\":" << transport_stats.requests
            << ",\"received_bytes\":" << transport_stats.received
            << ",\"failures\":" << transport_stats.failures
            << ",\"connections\":" << transport_stats.connections
            << ",\"read_microseconds\":" << transport_stats.read_microseconds
            << ",\"sync_opens\":" << sync_opens << ",\"async_opens\":" << async_opens
            << ",\"exists_calls\":" << exists_calls << ",\"timestamp_calls\":" << timestamp_calls
            << ",\"size_requests\":" << size_requests << ",\"read_requests\":" << read_requests
            << ",\"callbacks\":" << callbacks << ",\"callbacks_returned\":" << callbacks_returned
            << ",\"missing_callbacks\":" << missing_callbacks
            << ",\"canceled_callbacks\":" << canceled_callbacks
            << ",\"callback_failures\":" << callback_failures
            << ",\"async_reads\":{\"enabled\":" << async.enabled << ",\"pending\":" << async.pending
            << ",\"peak_pending\":" << async.peak_pending << ",\"queued\":" << async.queued
            << ",\"inline_reads\":" << async.inline_reads << ",\"completed\":" << async.completed
            << ",\"canceled\":" << async.canceled << '}' << ",\"last_size\":" << last_size
            << ",\"async_request_size\":" << kh02_async_request_size()
            << ",\"disk_cache\":false,\"file_count\":" << file_ids.size()
            << ",\"read_file_count\":" << transport_stats.read_files.size()
            << ",\"catalog_tag\":" << kh02asset::asset_index().tag
            << ",\"adaptation_error\":" << nlohmann::json(transport_stats.adaptation_error).dump()
            << ",\"adaptation_cache\":" << nlohmann::json{
                {"hits", transport_stats.adaptation_cache.hits},
                {"preparations", transport_stats.adaptation_cache.preparations},
                {"evictions", transport_stats.adaptation_cache.evictions},
                {"entries", transport_stats.adaptation_cache.entries},
                {"bytes", transport_stats.adaptation_cache.bytes}}.dump();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const auto timing = remote_transport().timing_snapshot();
        out << ",\"transport_timing\":{\"attempts\":" << timing.attempts
            << ",\"successful\":" << timing.successful
            << ",\"queue_wait_us\":" << timing.queue_wait_us
            << ",\"maximum_queue_wait_us\":" << timing.maximum_queue_wait_us
            << ",\"service_us\":" << timing.service_us
            << ",\"maximum_service_us\":" << timing.maximum_service_us;
        const auto write_histogram = [&](const char* name, const auto& values)
        {
            out << ",\"" << name << "\":[";
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                if (index != 0)
                {
                    out << ',';
                }
                out << values[index];
            }
            out << ']';
        };
        write_histogram("bucket_upper_bounds_us", kh02asset::rpc_latency_bounds_us);
        write_histogram("queue_histogram", timing.queue_histogram);
        write_histogram("service_histogram", timing.service_histogram);
        const auto transfer = remote_transport().transfer_timing_snapshot();
        out << ",\"request_write_us\":" << transfer.request_write_us
            << ",\"response_header_us\":" << transfer.response_header_us
            << ",\"response_payload_us\":" << transfer.response_payload_us
            << ",\"relocate_us\":" << transfer.relocate_us
            << ",\"read_calls\":" << transfer.read_calls
            << ",\"write_calls\":" << transfer.write_calls;
        out << '}';
#endif
        out << ",\"voice_language\":\"" << bound_voice_language
            << "\",\"voice_pairs\":" << kh02asset::asset_index().voice_pairs.size()
            << ",\"voice_reads\":[";
        bool first_voice = true;
        for (const auto file : transport_stats.read_files)
        {
            if (!paired_voice_files.contains(file))
            {
                continue;
            }
            if (!first_voice)
            {
                out << ',';
            }
            first_voice = false;
            const auto& source = kh02asset::asset_index().files[file].source;
            out << '\"';
            for (const auto character : source)
            {
                out << static_cast<char>(character);
            }
            out << '\"';
        }
        out << ']';
        const auto write_cache = [&](const char* name, const kh02asset::ReadCacheStats& stats,
                                     size_t budget, size_t maximum, bool enabled,
                                     const char* maximum_name = "max_file_bytes",
                                     size_t header_limit = 0, size_t entry_limit = 0)
        {
            out << ",\"" << name << "\":{\"bytes\":" << stats.bytes << ",\"hits\":" << stats.hits
                << ",\"misses\":" << stats.misses << ",\"evictions\":" << stats.evictions
                << ",\"reused_bytes\":" << stats.reused_bytes << ",\"budget_bytes\":" << budget
                << ",\"" << maximum_name << "\":" << maximum << ",\"enabled\":" << enabled;
            if (header_limit)
            {
                out << ",\"max_header_bytes\":" << header_limit;
            }
            if (entry_limit)
            {
                out << ",\"max_entries\":" << entry_limit;
            }
            out << '}';
        };
        write_cache("header_cache", headers, kh02asset::header_cache_budget,
                    kh02asset::header_cache_max_file, true);
        write_cache("export_cache", exports, kh02asset::export_cache_budget,
                    kh02asset::export_cache_max_file, exports_enabled);
        write_cache("range_cache", ranges, kh02asset::range_cache_budget,
                    kh02asset::range_cache_max_read, ranges_enabled, "max_read_bytes",
                    kh02asset::range_cache_max_header, kh02asset::range_cache_max_entries);
        if (native_adapter().installed() && native_adapter().native_object())
        {
            out << ",\"native_assets\":{";
            bool first = true;
            for (const auto& [name, path] : std::array{
                     std::pair{"postprocess_ev",
                               L"../../../KINGDOM HEARTS "
                               L"III/Content/Maps/Common/Materials/PP_CHR_ENV_EV.uasset"},
                     std::pair{"postprocess_lut",
                               L"../../../KINGDOM HEARTS "
                               L"III/Content/Maps/Common/Materials/PP_CHR_ENV_LUT.uasset"},
                     std::pair{"gear_material",
                               L"../../../KINGDOM HEARTS "
                               L"III/Content/Maps/dw/gimmick/g_dw_GearsA/mdl/M_cd_GearsA.uasset"},
                     std::pair{"event_aqua",
                               L"../../../KINGDOM HEARTS "
                               L"III/Content/Blueprints/EventPawn/ex/p_ex002_0_Event.uasset"}})
            {
                if (!first)
                {
                    out << ',';
                }
                first = false;
                out << '\"' << name << "\":"
                    << native_adapter().forward_exists(native_adapter().native_object(), path);
            }
            out << '}';
        }
        out << '}';
        return out.str();
    }

    void bind_kh02_remote_files(uint32_t pid)
    {
        native_adapter().install(pid,
                                 {file_exists, file_size, timestamp, stat_data, open_read,
                                  open_async, prepare_native_binding, rollback_native_binding});
    }

    auto enable_kh02_remote_files(uint32_t pid) -> std::string
    {
        bind_kh02_remote_files(pid);
        return kh02_remote_files_status();
    }
} // namespace kh3ap
