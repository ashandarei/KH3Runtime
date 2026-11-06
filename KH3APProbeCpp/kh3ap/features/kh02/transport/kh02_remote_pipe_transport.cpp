#include "kh02_remote_pipe_transport.h"
#include "kh02_asset_protocol.h"
#include "kh02_index_state.h"
#include "kh02_package_adapter.h"
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr DWORD pipe_operation_timeout_ms = 5'000;
        constexpr int pipe_connection_attempt_limit = 100;
        constexpr DWORD pipe_busy_wait_ms = 25;
        constexpr DWORD pipe_connection_retry_delay_ms = 2;
    } // namespace

    struct Kh02RemotePipeTransport::Impl
    {
        std::atomic<uint32_t> provider{};
        std::atomic<uint64_t> binding{};
        std::atomic<uint64_t> sequence{}, requests{}, received{}, failures{}, connections{},
            read_microseconds{};
        std::wstring pipe_path;
        mutable std::mutex transport_mutex;
        mutable std::mutex stats_mutex;
        std::set<uint32_t> read_files;
        kh02asset::PackageAdapter adapter;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        kh02asset::RpcTimingMetrics transport_timing;
        std::atomic<uint64_t> request_write_us{}, response_header_us{}, response_payload_us{},
            relocate_us{};
        std::atomic<uint64_t> read_calls{}, write_calls{};
#endif
        struct Transport
        {
            HANDLE pipe = INVALID_HANDLE_VALUE, event = nullptr;

            ~Transport()
            {
                reset();
                if (event)
                {
                    CloseHandle(event);
                }
            }

            void reset()
            {
                if (pipe != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(pipe);
                }
                pipe = INVALID_HANDLE_VALUE;
            }
        } transport;

        bool transfer(HANDLE pipe, void* data, DWORD count, bool write)
        {
            DWORD done = 0;
            while (done < count)
            {
                OVERLAPPED operation{};
                operation.hEvent = transport.event;
                if (!operation.hEvent)
                {
                    return false;
                }
                ResetEvent(operation.hEvent);
                DWORD step = 0;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                if (write)
                {
                    ++write_calls;
                }
                else
                {
                    ++read_calls;
                }
#endif
                bool ok = (write ? WriteFile(pipe, static_cast<unsigned char*>(data) + done,
                                             count - done, &step, &operation)
                                 : ReadFile(pipe, static_cast<unsigned char*>(data) + done,
                                            count - done, &step, &operation)) != 0;
                if (!ok && GetLastError() == ERROR_IO_PENDING)
                {
                    if (WaitForSingleObject(operation.hEvent, pipe_operation_timeout_ms) ==
                        WAIT_OBJECT_0)
                    {
                        ok = GetOverlappedResult(pipe, &operation, &step, false) != 0;
                    }
                    else
                    {
                        CancelIoEx(pipe, &operation);
                        GetOverlappedResult(pipe, &operation, &step, true);
                        ok = false;
                    }
                }
                if (!ok || !step)
                {
                    return false;
                }
                done += step;
            }
            return true;
        }

        bool read(uint32_t file, uint64_t offset, uint32_t length, void* output, bool reconnect,
                  uint64_t* index_size = nullptr)
        {
            using namespace kh02asset;
            const auto& assets = asset_index();
            const auto pid = provider.load();
            const bool metadata = file == index_file_id;
            if (!pid || !assets.tag || length > max_read || (length && !output) ||
                (!metadata && (file >= assets.files.size() || offset > assets.files[file].size ||
                               length > assets.files[file].size - offset)))
            {
                return false;
            }
            const auto began = std::chrono::steady_clock::now();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            kh02asset::RpcTimingObservation timing(transport_timing);
#endif
            std::lock_guard transport_lock(transport_mutex);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            timing.lock_acquired();
#endif
            if (reconnect)
            {
                transport.reset();
            }
            if (!transport.event)
            {
                transport.event = CreateEventW(nullptr, true, false, nullptr);
            }
            if (!transport.event)
            {
                ++failures;
                return false;
            }
            if (transport.pipe == INVALID_HANDLE_VALUE)
            {
                for (int retry = 0; retry < pipe_connection_attempt_limit; ++retry)
                {
                    transport.pipe =
                        CreateFileW(pipe_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                    if (transport.pipe != INVALID_HANDLE_VALUE)
                    {
                        break;
                    }
                    const auto error = GetLastError();
                    if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND)
                    {
                        break;
                    }
                    WaitNamedPipeW(pipe_path.c_str(), pipe_busy_wait_ms);
                    Sleep(pipe_connection_retry_delay_ms);
                }
                if (transport.pipe == INVALID_HANDLE_VALUE)
                {
                    ++failures;
                    return false;
                }
                ULONG owner = 0;
                if (!GetNamedPipeServerProcessId(transport.pipe, &owner) || owner != pid)
                {
                    transport.reset();
                    ++failures;
                    return false;
                }
                ++connections;
            }
            const auto pipe = transport.pipe;
            Request request{magic, version, file, length, offset, ++sequence, assets.tag};
            Response response{};
            ++requests;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            auto phase_started = std::chrono::steady_clock::now();
            const auto record_phase = [&](std::atomic<uint64_t>& total)
            {
                const auto now = std::chrono::steady_clock::now();
                total += std::chrono::duration_cast<std::chrono::microseconds>(now - phase_started)
                             .count();
                phase_started = now;
            };
#endif
            const bool request_sent = transfer(pipe, &request, sizeof(request), true);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            record_phase(request_write_us);
#endif
            const bool header_received =
                request_sent && transfer(pipe, &response, sizeof(response), false);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            record_phase(response_header_us);
#endif
            if (!header_received || response.magic != magic || response.status ||
                response.pid != pid || response.length != length ||
                (metadata ? (!response.file_size || response.file_size > maximum_index_bytes ||
                             offset > response.file_size || length > response.file_size - offset)
                          : response.file_size != assets.files[file].size) ||
                response.catalog_tag != assets.tag || response.offset != offset ||
                response.nonce != request.nonce)
            {
                transport.reset();
                ++failures;
                return false;
            }
            const bool payload_received = !length || transfer(pipe, output, length, false);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            record_phase(response_payload_us);
#endif
            if (!payload_received)
            {
                transport.reset();
                ++failures;
                return false;
            }
            if (index_size)
            {
                *index_size = response.file_size;
            }
            if (length && !metadata)
            {
                std::lock_guard lock(stats_mutex);
                read_files.insert(file);
            }
            received += length;
            read_microseconds += std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - began)
                                     .count();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            timing.succeeded();
#endif
            return true;
        }
    };

    Kh02RemotePipeTransport::Kh02RemotePipeTransport() : impl(std::make_unique<Impl>())
    {
    }

    Kh02RemotePipeTransport::~Kh02RemotePipeTransport() = default;

    void Kh02RemotePipeTransport::bind(uint32_t source_pid)
    {
        ++impl->binding;
        impl->provider = 0;
        impl->adapter.clear();
        std::lock_guard lock(impl->transport_mutex);
        impl->transport.reset();
        impl->provider = source_pid;
        impl->pipe_path = L"\\\\.\\pipe\\KH02AssetRead_" + std::to_wstring(source_pid);
    }

    void Kh02RemotePipeTransport::disable()
    {
        ++impl->binding;
        impl->provider = 0;
    }

    void Kh02RemotePipeTransport::load_index()
    {
        using namespace kh02asset;
        uint64_t size{};
        if (!impl->read(index_file_id, 0, 0, nullptr, false, &size))
        {
            throw std::runtime_error("kh02_remote_index_size");
        }
        std::string wire(static_cast<std::size_t>(size), '\0');
        for (uint64_t offset = 0; offset < size;)
        {
            const auto length = static_cast<uint32_t>(std::min<uint64_t>(max_read, size - offset));
            uint64_t advertised{};
            if (!impl->read(index_file_id, offset, length, wire.data() + offset, false,
                            &advertised) ||
                advertised != size)
            {
                throw std::runtime_error("kh02_remote_index_transfer");
            }
            offset += length;
        }
        publish_index(parse_index(wire, asset_index().source_build));
    }

    auto Kh02RemotePipeTransport::provider_pid() const -> uint32_t
    {
        return impl->provider.load();
    }

    auto Kh02RemotePipeTransport::connection_count() const -> uint64_t
    {
        return impl->connections.load();
    }

    auto Kh02RemotePipeTransport::read(uint32_t file, uint64_t offset, uint32_t length,
                                       void* output, bool reconnect) -> bool
    {
        const auto binding = impl->binding.load();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const auto began = std::chrono::steady_clock::now();
        std::chrono::steady_clock::duration raw_time{};
#endif
        const auto result =
            impl->adapter.read(file, offset, length, output,
                               [&](uint32_t id, uint64_t at, uint32_t size, void* destination)
                               {
                                   if (impl->binding.load() != binding)
                                   {
                                       return false;
                                   }
                                   const bool reset = std::exchange(reconnect, false);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                                   const auto raw_began = std::chrono::steady_clock::now();
#endif
                                   const auto received =
                                       impl->read(id, at, size, destination, reset);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                                   raw_time += std::chrono::steady_clock::now() - raw_began;
#endif
                                   return received;
                               });
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const auto adaptation = std::chrono::steady_clock::now() - began - raw_time;
        impl->relocate_us +=
            std::chrono::duration_cast<std::chrono::microseconds>(adaptation).count();
#endif
        return result && impl->binding.load() == binding;
    }

    auto Kh02RemotePipeTransport::snapshot() const -> Stats
    {
        std::lock_guard lock(impl->stats_mutex);
        return {impl->provider.load(),
                impl->requests.load(),
                impl->received.load(),
                impl->failures.load(),
                impl->connections.load(),
                impl->read_microseconds.load(),
                {impl->read_files.begin(), impl->read_files.end()},
                impl->adapter.last_error(), impl->adapter.snapshot()};
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02RemotePipeTransport::transfer_timing_snapshot() const -> TransferTiming
    {
        return {impl->request_write_us.load(),    impl->response_header_us.load(),
                impl->response_payload_us.load(), impl->relocate_us.load(),
                impl->read_calls.load(),          impl->write_calls.load()};
    }

    auto Kh02RemotePipeTransport::read_original(uint32_t file, uint64_t offset, uint32_t length, void* output) -> bool
    {
        return impl->read(file, offset, length, output, false);
    }

    auto Kh02RemotePipeTransport::timing_snapshot() const -> kh02asset::RpcTimingTotals
    {
        return impl->transport_timing.snapshot();
    }
#endif
} // namespace kh3ap
