#pragma once

#include <filesystem>
#include <functional>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    inline constexpr auto he02_first_save_point_story_threshold = 300;

    class RuntimeState;

    constexpr uint32_t s_runtime_auto_save_point_max_attempts = 2;

    [[nodiscard]] constexpr auto runtime_he02_save_point_needed(int32_t gameflow_he) noexcept
        -> bool
    {
        return gameflow_he < he02_first_save_point_story_threshold;
    }

    [[nodiscard]] constexpr auto
    runtime_save_point_playable_context_matches_pawn(std::wstring_view hook,
                                                     const void* latest_playable_object,
                                                     const void* pawn_object) noexcept -> bool
    {
        return hook == L"RuntimePlayablePawnContext" && latest_playable_object != nullptr &&
               latest_playable_object == pawn_object;
    }

    [[nodiscard]] constexpr auto
    should_retry_runtime_save_point_auto_spawn(bool spawned, bool class_not_loaded,
                                               bool class_load_requested_now,
                                               uint32_t attempt_count) noexcept -> bool
    {
        return !spawned &&
               (class_load_requested_now ||
                (!class_not_loaded && attempt_count < s_runtime_auto_save_point_max_attempts));
    }

    struct RuntimeAutoSavePointSpawnState
    {
        std::wstring last_context_key{};
        std::chrono::steady_clock::time_point last_attempt_at{};
        uint32_t attempt_count{};
        bool asset_load_requested{};
        bool terminal_failure{};
        // Historical name retained for compatibility. This becomes true only
        // after a successful spawn, not merely after attempting one.
        bool spawn_attempted{};
        RC::Unreal::UObject* spawned_actor{};
        std::wstring spawned_actor_name{};

        [[nodiscard]] auto already_spawned_for(std::wstring_view context_key) const -> bool
        {
            return spawn_attempted && last_context_key == context_key;
        }

        [[nodiscard]] auto finished_for(std::wstring_view context_key) const -> bool
        {
            return (spawn_attempted || terminal_failure) && last_context_key == context_key;
        }

        auto begin_context(std::wstring_view context_key) -> void
        {
            if (last_context_key == context_key)
            {
                return;
            }
            *this = RuntimeAutoSavePointSpawnState{};
            last_context_key = context_key;
        }

        auto reset() -> void
        {
            *this = RuntimeAutoSavePointSpawnState{};
        }
    };

    struct RuntimeSavePointSpawnDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view events_filename;
        std::wstring_view protocol_name;
        int protocol_version{};
        std::wstring_view build_stamp;
        std::wstring_view save_point_class_path;
        std::wstring_view save_point_blueprint_path;
        std::wstring_view actor_location_function_path;
        std::wstring_view gameplay_statics_default_object_path;
        std::wstring_view load_game_from_slot_path;
        std::wstring_view create_save_game_object_path;
        std::wstring_view save_game_to_slot_path;
        std::wstring_view goa_save_point_data_class_path;
        std::wstring_view goa_save_point_data_blueprint_path;
        std::wstring_view default_player_start_tag;
        std::wstring_view default_my_reactor_path;
        std::wstring_view default_my_recover_path;
        std::wstring_view default_my_map_marker_path;
        bool enable_lifetime_deep_samples{};
        bool debug_logging{};
        // Evaluated only for an otherwise eligible automatic spawn, including
        // retries. A failed read can defer spawning without consuming an attempt.
        std::function<bool()> auto_spawn_allowed{};
    };

    struct GoASavePointRouteSyncSummary
    {
        bool requested{};
        bool loaded{};
        bool created{};
        bool saved{};
        std::wstring status{};
        std::wstring reason{};
        std::wstring slot_name{};
        std::wstring class_resolution_source{};
        std::wstring save_game_object{};
        std::wstring level_path{};
        std::wstring player_start{};
    };

    struct RuntimePlayerStartRouteSpawnResult
    {
        std::wstring status{};
        std::wstring reason{};
        std::wstring tag{};
        std::wstring class_resolution_source{};
        std::wstring actor{};
        std::wstring player_start_property{};
    };

    enum class RuntimePlayerStartSelectionDecision
    {
        Ignore,
        KeepSelected,
        OverrideSelected,
        RequestedActorUnavailable,
    };

    [[nodiscard]] constexpr auto decide_runtime_player_start_selection(
        std::wstring_view incoming_name, std::wstring_view requested_name, bool map_context_matches,
        bool requested_actor_available, bool selected_actor_matches_requested) noexcept
        -> RuntimePlayerStartSelectionDecision
    {
        if (!map_context_matches || incoming_name != requested_name)
        {
            return RuntimePlayerStartSelectionDecision::Ignore;
        }
        if (!requested_actor_available)
        {
            return RuntimePlayerStartSelectionDecision::RequestedActorUnavailable;
        }
        return selected_actor_matches_requested
                   ? RuntimePlayerStartSelectionDecision::KeepSelected
                   : RuntimePlayerStartSelectionDecision::OverrideSelected;
    }

    auto process_pending_runtime_save_point_spawn(const RuntimeSavePointSpawnDeps& deps) -> void;

    auto spawn_runtime_player_start_for_route(RC::Unreal::UObject* world_context,
                                              std::wstring_view player_start_tag,
                                              std::wstring_view disp_map_name, double x, double y,
                                              double z, double yaw,
                                              RuntimePlayerStartRouteSpawnResult* result)
        -> RC::Unreal::UObject*;

    auto sync_goa_save_point_route(const RuntimeSavePointSpawnDeps& deps,
                                   std::wstring_view slot_name,
                                   std::wstring_view level_path_property,
                                   std::wstring_view player_start_property,
                                   std::wstring_view level_path, std::wstring_view player_start,
                                   GoASavePointRouteSyncSummary* summary) -> bool;

    auto process_runtime_map_save_point_auto_spawn(
        RuntimeAutoSavePointSpawnState& state, const RuntimeSavePointSpawnDeps& deps,
        std::chrono::steady_clock::time_point now, std::wstring_view source_name,
        std::wstring_view request_id_prefix, std::wstring_view map_path,
        std::wstring_view player_start_tag, std::wstring_view disp_map_name, double x, double y,
        double z, std::chrono::steady_clock::duration stable_delay,
        std::chrono::steady_clock::duration retry_interval,
        std::wstring_view goa_save_point_slot_name,
        std::wstring_view goa_save_point_level_path_property,
        std::wstring_view goa_save_point_player_start_property) -> void;
} // namespace kh3ap
