#pragma once
#include "kh3ap/core/performance_build.h"
#include "kh02_package_adapter.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh02_rpc_metrics.h"
#endif
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kh3ap
{
    class Kh02RemotePipeTransport
    {
    public:
        struct Stats
        {
            uint32_t provider_pid{};
            uint64_t requests{}, received{}, failures{}, connections{}, read_microseconds{};
            std::vector<uint32_t> read_files;
            std::string adaptation_error;
            kh02asset::PackageAdapter::Stats adaptation_cache;
        };

        Kh02RemotePipeTransport();
        ~Kh02RemotePipeTransport();
        void bind(uint32_t source_pid);
        void disable();
        void load_index();
        auto provider_pid() const -> uint32_t;
        auto connection_count() const -> uint64_t;
        auto read(uint32_t file, uint64_t offset, uint32_t length, void* output,
                  bool reconnect = false) -> bool;
        auto snapshot() const -> Stats;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto read_original(uint32_t file, uint64_t offset, uint32_t length, void* output) -> bool;
        struct TransferTiming
        {
            uint64_t request_write_us{}, response_header_us{}, response_payload_us{}, relocate_us{};
            uint64_t read_calls{}, write_calls{};
        };

        auto transfer_timing_snapshot() const -> TransferTiming;
        auto timing_snapshot() const -> kh02asset::RpcTimingTotals;
#endif

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
