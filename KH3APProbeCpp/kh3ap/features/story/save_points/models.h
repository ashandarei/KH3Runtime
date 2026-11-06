#pragma once
#include "kh3ap/features/story/save_points/save_point_spawn_helpers.h"
#include "kh3ap/features/story/save_points/activation.h"
#include <chrono>
#include <optional>
#include <string>
#include <vector>
namespace kh3ap::save_point_detail
{
    using RC::Unreal::UObject;
    using save_point_activation::ActivationCallSummary;
    struct SpawnVector
    {
        double x{};
        double y{};
        double z{};
    };

    struct SavePointSpawnRequest
    {
        std::wstring request_id{};
        std::wstring player_start_tag{};
        std::wstring runtime_player_start_tag{};
        std::wstring disp_map_name{};
        std::wstring my_reactor_path{};
        std::wstring my_recover_path{};
        std::wstring my_map_marker_path{};
        std::optional<SpawnVector> location{};
        bool is_auto_save{true};
        bool force_activate{true};
        bool force_component_engine_activation{true};
        bool disable_blocking_collision{true};
        bool spawn_runtime_player_start{false};
        bool use_runtime_player_start_tag_for_save_point{true};
        bool sync_goa_save_point_slot{false};
        std::wstring goa_save_point_slot_name{};
        std::wstring goa_save_point_level_path_property{};
        std::wstring goa_save_point_player_start_property{};
        std::wstring goa_save_point_level_path{};
        std::wstring goa_save_point_player_start{};
    };

    struct ComponentAssignmentSummary
    {
        std::wstring property_name{};
        std::wstring requested_path{};
        std::wstring lookup_path{};
        std::wstring status{};
        std::wstring reason{};
        std::wstring resolved_object{};
        std::wstring final_value{};
        bool wrote{};
    };

    struct MetadataWriteSummary
    {
        std::wstring stage{};
        std::wstring property_name{};
        std::wstring requested_value{};
        std::wstring status{};
        std::wstring reason{};
        std::wstring final_value{};
        bool wrote{};
    };

    struct SavePointSetupSummary
    {
        std::vector<MetadataWriteSummary> metadata_writes{};
        std::vector<ActivationCallSummary> activation_calls{};
    };

    struct RuntimePlayerStartSpawnSummary
    {
        bool requested{};
        std::wstring status{};
        std::wstring reason{};
        std::wstring tag{};
        std::wstring class_resolution_source{};
        std::wstring actor{};
        std::wstring player_start_property{};
        std::vector<MetadataWriteSummary> metadata_writes{};
        std::vector<ActivationCallSummary> activation_calls{};
    };

    struct GoASavePointSyncSummary
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
        std::vector<MetadataWriteSummary> property_writes{};
    };

    struct SavePointSpawnWatch
    {
        std::wstring source{};
        std::wstring request_id{};
        UObject* actor{};
        std::wstring spawned_actor{};
        UObject* runtime_player_start{};
        std::wstring spawned_runtime_player_start{};
        std::chrono::steady_clock::time_point spawned_at{};
        size_t next_sample_index{};
    };
} // namespace kh3ap::save_point_detail
