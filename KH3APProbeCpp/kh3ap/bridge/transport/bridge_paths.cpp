#include <chrono>
#include <optional>
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/pipe_transport.h"

#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <limits>
#include <regex>
#include <system_error>
#include <utility>
#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr int ap_status_candidate_score = 30;
        constexpr int game_state_candidate_score = 20;
        constexpr int item_delivery_candidate_score = 15;
        constexpr int runtime_status_candidate_score = 10;
        constexpr int mod_local_candidate_penalty = 50;
    } // namespace

    BridgePathResolver::BridgePathResolver(std::wstring default_output_path,
                                           std::wstring config_path,
                                           std::chrono::steady_clock::duration cache_ttl,
                                           bool debug_logging)
        : m_default_output_path(std::move(default_output_path)),
          m_config_path(std::move(config_path)), m_cache_ttl(cache_ttl),
          m_debug_logging(debug_logging)
    {
    }

    auto BridgePathResolver::current_module_directory() -> std::optional<std::filesystem::path>
    {
        HMODULE module_handle = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&BridgePathResolver::current_module_directory),
                &module_handle) ||
            !module_handle)
        {
            return std::nullopt;
        }

        std::wstring buffer(static_cast<size_t>(MAX_PATH), L'\0');
        const DWORD length =
            GetModuleFileNameW(module_handle, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            return std::nullopt;
        }
        buffer.resize(length);
        return std::filesystem::path{buffer}.parent_path();
    }

    auto BridgePathResolver::parse_output_paths_from_config(const std::filesystem::path& path) const
        -> std::vector<std::filesystem::path>
    {
        std::vector<std::filesystem::path> output_paths{};

        const auto config_text = read_text_file(path);
        if (!config_text.has_value())
        {
            return output_paths;
        }

        const std::regex path_pattern(R"(\[\[([^\]]+)\]\])");
        std::smatch match{};
        std::string::const_iterator search_start = config_text->cbegin();
        while (std::regex_search(search_start, config_text->cend(), match, path_pattern))
        {
            output_paths.emplace_back(std::filesystem::path{match[1].str()});
            search_start = match.suffix().first;
        }

        return output_paths;
    }

    auto BridgePathResolver::normalize_output_path(std::filesystem::path path)
        -> std::filesystem::path
    {
        std::error_code ec{};
        if (path.is_relative())
        {
            path = std::filesystem::absolute(path, ec);
        }
        if (ec)
        {
            return path;
        }
        return path.lexically_normal();
    }

    auto BridgePathResolver::is_probable_mod_local_bridge_dir(const std::filesystem::path& path)
        -> bool
    {
        const std::wstring value = path.lexically_normal().wstring();
        return value.find(L"\\Mods\\KH3APProbeCpp\\") != std::wstring::npos;
    }

    auto BridgePathResolver::path_points_to_regular_file(const std::filesystem::path& path) -> bool
    {
        if (pipe::manages(path))
        {
            return pipe::revision(path) != 0;
        }
        std::error_code ec{};
        return std::filesystem::is_regular_file(path, ec) && !ec;
    }

    auto BridgePathResolver::score_bridge_dir_candidate(const std::filesystem::path& path) -> int
    {
        int score = 0;

        if (path_points_to_regular_file(path / channels::ap_status))
        {
            score += ap_status_candidate_score;
        }
        if (path_points_to_regular_file(path / channels::game_state))
        {
            score += game_state_candidate_score;
        }
        if (path_points_to_regular_file(path / channels::item_delivery))
        {
            score += item_delivery_candidate_score;
        }
        if (path_points_to_regular_file(path / "bridge_runtime_status.json"))
        {
            score += runtime_status_candidate_score;
        }
        if (is_probable_mod_local_bridge_dir(path))
        {
            score -= mod_local_candidate_penalty;
        }

        return score;
    }

    auto BridgePathResolver::output_paths() -> const std::vector<std::filesystem::path>&
    {
        std::call_once(m_output_paths_once,
                       [this]()
                       {
                           std::vector<std::filesystem::path> paths =
                               parse_output_paths_from_config(std::filesystem::path{m_config_path});
                           if (paths.empty())
                           {
                               paths.emplace_back(std::filesystem::path{m_default_output_path});
                           }
                           for (auto& path : paths)
                           {
                               path = normalize_output_path(std::move(path));
                           }
                           m_output_paths = std::move(paths);
                       });

        return m_output_paths;
    }

    auto BridgePathResolver::try_get_bridge_dir() -> std::optional<std::filesystem::path>
    {
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock{m_cache_mutex};
            if (m_cached_bridge_dir_at.time_since_epoch().count() != 0 &&
                (now - m_cached_bridge_dir_at) < m_cache_ttl)
            {
                return m_cached_bridge_dir;
            }
        }

        const auto cache_and_return = [this, now](std::optional<std::filesystem::path> value)
            -> std::optional<std::filesystem::path>
        {
            std::lock_guard lock{m_cache_mutex};
            m_cached_bridge_dir = value;
            m_cached_bridge_dir_at = now;
            return value;
        };

        std::optional<std::filesystem::path> best_scored_dir{};
        int best_score = std::numeric_limits<int>::min();
        std::optional<std::filesystem::path> fallback_non_mod_dir{};
        std::optional<std::filesystem::path> fallback_any_dir{};

        for (const auto& output_path : output_paths())
        {
            if (output_path.filename() == channels::runtime_loot_event_file)
            {
                const auto candidate_dir = output_path.parent_path();
                const int candidate_score = score_bridge_dir_candidate(candidate_dir);

                if (m_debug_logging)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Bridge dir candidate path={} | score={} | "
                            "mod_local={}\n"),
                        candidate_dir.wstring(), candidate_score,
                        is_probable_mod_local_bridge_dir(candidate_dir) ? STR("true")
                                                                        : STR("false"));
                }

                if (!fallback_any_dir.has_value())
                {
                    fallback_any_dir = candidate_dir;
                }
                if (!is_probable_mod_local_bridge_dir(candidate_dir) &&
                    !fallback_non_mod_dir.has_value())
                {
                    fallback_non_mod_dir = candidate_dir;
                }
                if (candidate_score > best_score)
                {
                    best_score = candidate_score;
                    best_scored_dir = candidate_dir;
                }
            }
        }

        if (best_scored_dir.has_value() && best_score > 0)
        {
            if (m_debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Bridge dir selected via score path={} | score={}\n"),
                    best_scored_dir->wstring(), best_score);
            }
            return cache_and_return(best_scored_dir);
        }

        if (fallback_non_mod_dir.has_value())
        {
            if (m_debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Bridge dir fallback from non-mod candidate path={}\n"),
                    fallback_non_mod_dir->wstring());
            }
            return cache_and_return(fallback_non_mod_dir);
        }

        if (fallback_any_dir.has_value())
        {
            if (m_debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Bridge dir fallback from first candidate path={}\n"),
                    fallback_any_dir->wstring());
            }
            return cache_and_return(fallback_any_dir);
        }

        if (m_debug_logging)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Bridge dir candidate not found from output paths\n"));
        }
        return cache_and_return(std::nullopt);
    }

    auto try_get_current_module_directory() -> std::optional<std::filesystem::path>
    {
        return BridgePathResolver::current_module_directory();
    }

    auto path_points_to_regular_file(const std::filesystem::path& path) -> bool
    {
        return BridgePathResolver::path_points_to_regular_file(path);
    }
} // namespace kh3ap
