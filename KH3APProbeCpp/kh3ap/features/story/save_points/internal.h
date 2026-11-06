#pragma once
#include "kh3ap/features/story/save_points/models.h"
#include "kh3ap/features/story/save_points/helpers.h"
#include "kh3ap/features/story/save_points/text.h"
#include <array>
#include <optional>
#include <sstream>
#include <utility>
namespace kh3ap::save_point_detail
{
    inline constexpr size_t runtime_name_suffix_limit = 32;
    using RC::Unreal::UClass;
    using RC::Unreal::UFunction;
    auto parse_request(const std::string& text, const RuntimeSavePointSpawnDeps& deps)
        -> SavePointSpawnRequest;

    auto choose_runtime_player_start_tag(const SavePointSpawnRequest& request) -> std::wstring;

    auto write_bool_metadata_property(UObject* actor, std::wstring_view stage,
                                      std::wstring_view property_name, bool value)
        -> MetadataWriteSummary;

    auto write_name_metadata_property(UObject* actor, std::wstring_view stage,
                                      std::wstring_view property_name, std::wstring_view value)
        -> MetadataWriteSummary;

    auto append_save_point_metadata_writes(SavePointSetupSummary* setup_summary, UObject* actor,
                                           const SavePointSpawnRequest& request,
                                           std::wstring_view stage) -> void;

    auto append_runtime_player_start_metadata_writes(RuntimePlayerStartSpawnSummary* summary,
                                                     UObject* actor,
                                                     const SavePointSpawnRequest& request,
                                                     std::wstring_view tag, std::wstring_view stage)
        -> void;

    auto try_write_player_start_tag(UObject* actor, std::wstring_view value) -> std::wstring;

    auto try_get_actor_location(UObject* pawn, std::wstring_view actor_location_function_path,
                                std::wstring* failure_reason) -> std::optional<SpawnVector>;

    [[nodiscard]] auto try_get_retained_save_point_class() -> UClass*;

    auto retain_save_point_class(UClass* actor_class) -> bool;

    auto try_resolve_save_point_class(const RuntimeSavePointSpawnDeps& deps,
                                      std::wstring* resolution_source) -> UClass*;

    auto try_resolve_runtime_player_start_class(std::wstring* resolution_source) -> UClass*;

    auto try_get_gameplay_statics_default_object(std::wstring_view default_object_path,
                                                 UFunction* fallback_function) -> UObject*;

    auto try_read_bool_return_value(UFunction* function_object, void* params, bool fallback = true)
        -> bool;

    auto try_resolve_goa_save_point_data_class(const RuntimeSavePointSpawnDeps& deps,
                                               std::wstring* resolution_source) -> UClass*;

    auto try_load_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                           std::wstring_view slot_name,
                                           std::wstring* failure_reason) -> UObject*;

    auto try_create_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                             std::wstring* class_resolution_source,
                                             std::wstring* failure_reason) -> UObject*;

    auto try_save_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                           UObject* save_game_object, std::wstring_view slot_name,
                                           std::wstring* failure_reason) -> bool;

    auto sync_goa_save_point_save_game(const RuntimeSavePointSpawnDeps& deps,
                                       const SavePointSpawnRequest& request,
                                       GoASavePointSyncSummary* summary) -> bool;

    auto ensure_component_property(UObject* actor, std::wstring_view property_name,
                                   std::wstring_view requested_path) -> ComponentAssignmentSummary;

    auto ensure_save_point_component_properties(UObject* actor,
                                                const SavePointSpawnRequest& request)
        -> std::array<ComponentAssignmentSummary, 3>;

    auto try_call_bool_return_function(UObject* object, std::wstring_view function_path)
        -> std::optional<bool>;

    auto try_call_object_return_function(UObject* object, std::wstring_view function_path)
        -> UObject*;

    auto collect_save_point_component_objects(UObject* actor)
        -> std::vector<std::pair<std::wstring_view, UObject*>>;

    auto collect_runtime_player_start_component_objects(UObject* actor)
        -> std::vector<std::pair<std::wstring_view, UObject*>>;

    auto prepare_runtime_player_start_actor(RuntimePlayerStartSpawnSummary* summary, UObject* actor)
        -> void;

    auto force_engine_component_state(SavePointSetupSummary* setup_summary, UObject* actor,
                                      const SavePointSpawnRequest& request) -> void;

    auto activate_save_point_actor(SavePointSetupSummary* setup_summary, UObject* actor,
                                   const SavePointSpawnRequest& request) -> void;

    auto append_property_snapshot_field(std::ostringstream& json, UObject* actor,
                                        std::wstring_view property_name, bool* first) -> void;

    auto append_component_state_json(std::ostringstream& json, std::wstring_view label,
                                     UObject* component, bool include_scene_state,
                                     bool include_primitive_state, bool include_tres_gimmick_state)
        -> void;

    auto append_actor_gimmick_state_json(std::ostringstream& json, UObject* actor) -> void;

    auto append_save_point_deep_state_json(std::ostringstream& json, UObject* actor) -> void;

    auto append_lifetime_sample(const RuntimeSavePointSpawnDeps& deps,
                                const SavePointSpawnWatch& watch, size_t sample_index,
                                std::chrono::milliseconds target_offset,
                                std::chrono::milliseconds elapsed) -> void;

    auto poll_save_point_spawn_watches(const RuntimeSavePointSpawnDeps& deps) -> void;

    auto append_setup_summary_json(std::ostringstream& json,
                                   const SavePointSetupSummary* setup_summary) -> void;

    auto append_goa_save_point_sync_summary_json(std::ostringstream& json,
                                                 const GoASavePointSyncSummary* sync_summary)
        -> void;

    auto write_result(const RuntimeSavePointSpawnDeps& deps, const SavePointSpawnRequest& request,
                      std::wstring_view status, std::wstring_view reason, UObject* pawn,
                      UObject* actor, const std::optional<SpawnVector>& location,
                      std::wstring_view class_resolution_source,
                      std::wstring_view player_start_property,
                      const RuntimePlayerStartSpawnSummary* runtime_player_start_summary,
                      const std::array<ComponentAssignmentSummary, 3>* component_assignments,
                      const SavePointSetupSummary* setup_summary,
                      const GoASavePointSyncSummary* goa_save_point_sync_summary) -> void;

    auto spawn_runtime_player_start(const SavePointSpawnRequest& request, UObject* world_context,
                                    const SpawnVector& location,
                                    RuntimePlayerStartSpawnSummary* summary, double yaw)
        -> UObject*;

    auto spawn_save_point(const RuntimeSavePointSpawnDeps& deps,
                          const SavePointSpawnRequest& request, UObject* pawn,
                          const SpawnVector& location, std::wstring* class_resolution_source,
                          std::wstring* player_start_property,
                          std::array<ComponentAssignmentSummary, 3>* component_assignments,
                          SavePointSetupSummary* setup_summary, std::wstring* failure_reason)
        -> UObject*;
    auto register_spawn_watch(SavePointSpawnWatch watch) -> void;
} // namespace kh3ap::save_point_detail
