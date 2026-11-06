#include "kh3ap/features/treasure/music_chest/music_chest_internal.h"
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <format>
#include <sstream>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace music_chest;
    using settings::s_enable_runtime_hot_path_profiling;

    void RuntimeMusicChestService::Impl::publish_status()
    {
        publication_work.request();
    }

    bool RuntimeMusicChestService::Impl::write_status()
    {
        if (!directory)
        {
            return true;
        }
        std::ostringstream out;
        out << "{\"schema\":\"kh3ap.music_chests.v1\",\"process_id\":" << GetCurrentProcessId()
            << ",\"assigned\":" << assignments.size() << ",\"loaded\":" << gates.size()
            << ",\"maintenance_passes\":" << maintenance_passes
            << ",\"maintenance_pending\":" << (maintenance_work.pending() ? "true" : "false")
            << ",\"lifecycle_listener\":" << (delete_listener_installed ? "true" : "false")
            << ",\"activation_hook\":" << (activation_hook ? "true" : "false")
            << ",\"activation_error\":\"" << activation_error
            << "\",\"activation_events\":" << activation_events
            << ",\"discovery_scans\":" << discovery_scans
            << ",\"discovery_actors\":" << discovery_actors
            << ",\"discovery_added\":" << discovery_added << ",\"error\":\""
            << json_escape(std::wstring(error.begin(), error.end()))
            << "\",\"callback_ready\":" << (retained_notice ? "true" : "false")
            << ",\"material_ready\":" << (retained_paint ? "true" : "false")
            << ",\"callback_load\":\"" << json_escape(notice_load_state)
            << "\",\"material_load\":\"" << json_escape(paint_load_state)
            << "\",\"placement_load\":\"" << json_escape(spawn_load_state)
            << "\",\"placement_attempts\":" << placed_initialization.attempts << ",\"gates\":[";
        bool first_gate = true;
        for (auto& [key, gate] : gates)
        {
            if (!first_gate)
            {
                out << ',';
            }
            first_gate = false;
            out << "{\"treasure_id\":\"" << json_escape(gate.treasure)
                << "\",\"bound\":" << (gate.bound ? "true" : "false")
                << ",\"painted\":" << (gate.painted ? "true" : "false")
                << ",\"required_keyblade\":\""
                << json_escape(assignments.at(gate.treasure).keyblade)
                << "\",\"has_required_keyblade\":" << (gate.keyblade_owned ? "true" : "false")
                << "}";
        }
        out << "],\"spawned\":[";
        bool first = true;
        for (auto& [key, gate] : gates)
        {
            if (gate.spawned)
            {
                if (!first)
                {
                    out << ',';
                }
                first = false;
                out << "{\"treasure_id\":\"" << json_escape(gate.treasure)
                    << "\",\"opened\":" << (gate.opened ? "true" : "false")
                    << ",\"opening\":" << (gate.opening ? "true" : "false") << ",\"save_error\":\""
                    << json_escape(std::wstring(gate.save_error.begin(), gate.save_error.end()))
                    << "\"}";
            }
        }
        out << "]}";
        if constexpr (s_enable_runtime_hot_path_profiling)
        {
            size_t bound{}, painted{};
            for (const auto& [key, gate] : gates)
            {
                bound += gate.bound;
                painted += gate.painted;
            }
            const auto detail =
                std::format("events={} scans={} examined={} added={} loaded={} bound={} painted={}",
                            activation_events, discovery_scans, discovery_actors, discovery_added,
                            gates.size(), bound, painted);
            if (detail != last_discovery_status)
            {
                last_discovery_status = detail;
                RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] ChestDiscovery {}\n"),
                                                       std::wstring(detail.begin(), detail.end()));
            }
        }
        const auto asset_status =
            std::format("callback={} material={} placement_assets={} attempts={} error={}",
                        retained_notice != nullptr, retained_paint != nullptr,
                        retained_spawn_assets.size(), placed_initialization.attempts, error);
        if (asset_status != last_asset_status)
        {
            last_asset_status = asset_status;
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Music chest initialization {} callback_load={} "
                    "material_load={} placement_load={}\n"),
                std::wstring(asset_status.begin(), asset_status.end()), notice_load_state,
                paint_load_state, spawn_load_state);
        }
        return status.publish(
            *directory / channels::runtime_music_chests_state_file, out.str(), Clock::now(),
            [](const auto& path)
            {
                return path_points_to_regular_file(path);
            },
            [](const auto& path, std::string_view text)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                    L"music_chests:status_write"};
                return write_text_file_atomic_verified(path, text);
            });
    }
} // namespace kh3ap
