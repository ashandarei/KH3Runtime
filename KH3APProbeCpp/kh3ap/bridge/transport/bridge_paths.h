#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    class BridgePathResolver
    {
    public:
        BridgePathResolver(
                std::wstring default_output_path,
                std::wstring config_path,
                std::chrono::steady_clock::duration cache_ttl,
                bool debug_logging);

        auto output_paths() -> const std::vector<std::filesystem::path>&;
        auto try_get_bridge_dir() -> std::optional<std::filesystem::path>;

        static auto current_module_directory() -> std::optional<std::filesystem::path>;
        static auto path_points_to_regular_file(const std::filesystem::path& path) -> bool;

    private:
        auto parse_output_paths_from_config(const std::filesystem::path& path) const -> std::vector<std::filesystem::path>;
        static auto normalize_output_path(std::filesystem::path path) -> std::filesystem::path;
        static auto is_probable_mod_local_bridge_dir(const std::filesystem::path& path) -> bool;
        static auto score_bridge_dir_candidate(const std::filesystem::path& path) -> int;

        std::wstring m_default_output_path;
        std::wstring m_config_path;
        std::chrono::steady_clock::duration m_cache_ttl;
        bool m_debug_logging{false};
        std::once_flag m_output_paths_once{};
        std::vector<std::filesystem::path> m_output_paths{};
        std::mutex m_cache_mutex{};
        std::optional<std::filesystem::path> m_cached_bridge_dir{};
        std::chrono::steady_clock::time_point m_cached_bridge_dir_at{};
    };

    auto try_get_current_module_directory() -> std::optional<std::filesystem::path>;
    auto path_points_to_regular_file(const std::filesystem::path& path) -> bool;
}
