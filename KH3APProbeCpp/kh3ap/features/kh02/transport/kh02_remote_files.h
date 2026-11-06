#pragma once
#include <cstdint>
#include <string>

namespace kh3ap
{
    // Run on the existing provider-preparation worker before native binding.
    void prepare_kh02_asset_index(uint32_t source_pid);
    auto kh02_remote_files_benchmark() -> std::string;
    void bind_kh02_remote_files(uint32_t source_pid);
    auto enable_kh02_remote_files(uint32_t source_pid) -> std::string;
    auto kh02_remote_files_status() -> std::string;
    auto configure_kh02_export_cache(bool enabled) -> std::string;
    auto configure_kh02_async_reads(bool enabled) -> std::string;
    auto configure_kh02_range_cache(bool enabled) -> std::string;
} // namespace kh3ap
