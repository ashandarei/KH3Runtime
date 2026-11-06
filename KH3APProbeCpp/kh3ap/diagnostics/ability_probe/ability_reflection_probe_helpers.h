#pragma once

#include "kh3ap/diagnostics/ability_probe/ability_reflection_models.h"

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    struct FFrame;
    struct FName;
    class FProperty;
    class FStructProperty;
    class UClass;
    class UObject;
    class UFunction;
    class UStruct;
    class UScriptStruct;
} // namespace RC::Unreal

namespace kh3ap
{
    inline constexpr auto default_ability_probe_entry_limit = 100;
    inline constexpr auto default_ability_probe_object_limit = 24;
    inline constexpr auto default_ability_probe_property_limit = 16;

    auto collect_ability_state_probe_candidates(RC::Unreal::UObject* preferred_object,
                                                RC::Unreal::UObject* last_world_context_object)
        -> std::vector<RC::Unreal::UObject*>;

    auto append_ability_discovery_candidate(std::vector<RC::Unreal::UObject*>& candidates,
                                            RC::Unreal::UObject* object) -> void;

    auto is_live_ability_owner_candidate(RC::Unreal::UObject* object) -> bool;

    auto should_probe_live_ability_owner_property(std::wstring_view property_name,
                                                  std::wstring_view property_class) -> bool;

    auto is_equipmentish_ability_owner_object(RC::Unreal::UObject* object) -> bool;

    auto should_probe_live_equipmentish_property(std::wstring_view property_name,
                                                 std::wstring_view property_class) -> bool;

    auto format_live_ability_owner_array_value(RC::Unreal::FProperty* property,
                                               const void* value_ptr) -> std::wstring;

    auto should_follow_live_ability_owner_object_property(std::wstring_view property_name,
                                                          RC::Unreal::UObject* value) -> bool;

    auto collect_live_ability_owner_candidates(RC::Unreal::UObject* preferred_object,
                                               RC::Unreal::UObject* last_world_context_object)
        -> std::vector<RC::Unreal::UObject*>;

    auto try_get_ability_array_needle_name(uint64_t raw_value) -> std::wstring_view;

    auto is_ability_array_needle_candidate(RC::Unreal::UObject* object) -> bool;

    auto collect_ability_array_needle_candidates(RC::Unreal::UObject* preferred_object,
                                                 RC::Unreal::UObject* last_world_context_object)
        -> std::vector<RC::Unreal::UObject*>;

    auto try_format_ability_array_needle_value(RC::Unreal::FProperty* property,
                                               const void* value_ptr)
        -> std::optional<std::pair<std::wstring, std::wstring>>;

    auto try_format_manual_byte_ability_array_needle_value(const ArrayHeader& header)
        -> std::optional<std::pair<std::wstring, std::wstring>>;

    auto should_probe_ability_scalar_or_map_property(std::wstring_view property_name) -> bool;

    auto try_format_ability_scalar_needle_value(RC::Unreal::FProperty* property,
                                                const void* value_ptr, int element_size)
        -> std::optional<std::pair<std::wstring, std::wstring>>;

    auto try_format_ability_map_needle_value(RC::Unreal::FProperty* property, const void* value_ptr)
        -> std::optional<std::pair<std::wstring, std::wstring>>;

    auto prepare_ability_scalar_needle_match_entry(
        RC::Unreal::UObject* object, RC::Unreal::UClass* object_class,
        std::wstring_view property_path, RC::Unreal::FProperty* property, const void* value_ptr,
        int absolute_offset, int element_size) -> std::optional<AbilityArrayNeedleProbeEntry>;

    auto prepare_ability_map_needle_match_entry(
        RC::Unreal::UObject* object, RC::Unreal::UClass* object_class,
        std::wstring_view property_path, RC::Unreal::FProperty* property, const void* value_ptr,
        int absolute_offset, int element_size) -> std::optional<AbilityArrayNeedleProbeEntry>;

    auto prepare_ability_array_needle_match_entry(
        RC::Unreal::UObject* object, RC::Unreal::UClass* object_class,
        std::wstring_view property_path, RC::Unreal::FProperty* property, const void* value_ptr,
        int absolute_offset, int element_size) -> std::optional<AbilityArrayNeedleProbeEntry>;

    auto prepare_manual_chrinit_sora_ability_array_entries(RC::Unreal::UObject* object,
                                                           RC::Unreal::UClass* object_class)
        -> std::vector<AbilityArrayNeedleProbeEntry>;

    auto prepare_ability_array_needle_struct_match_entries(
        RC::Unreal::UObject* object, RC::Unreal::UClass* object_class,
        std::wstring_view outer_property_name, RC::Unreal::FStructProperty* struct_property,
        const std::byte* struct_base, int outer_offset, int max_entries)
        -> std::vector<AbilityArrayNeedleProbeEntry>;

    auto prepare_ability_array_needle_object_match_entries(RC::Unreal::UObject* object,
                                                           RC::Unreal::UClass* object_class,
                                                           int max_entries)
        -> std::vector<AbilityArrayNeedleProbeEntry>;

    auto prepare_ability_array_needle_probe_entries(RC::Unreal::UObject* preferred_object,
                                                    RC::Unreal::UObject* last_world_context_object)
        -> std::vector<AbilityArrayNeedleProbeEntry>;

    auto format_manual_object_pointer_value(const void* value_ptr) -> std::wstring;

    auto read_manual_object_pointer(const void* value_ptr) -> RC::Unreal::UObject*;

    auto format_manual_array_at_offset(const std::byte* object_bytes, size_t offset,
                                       int assumed_element_size) -> std::wstring;

    auto format_manual_map_at_property(RC::Unreal::UObject* object, std::wstring_view property_name,
                                       size_t raw_entry_size) -> std::wstring;

    auto format_manual_map_nested_arrays_at_property(RC::Unreal::UObject* object,
                                                     std::wstring_view property_name)
        -> std::wstring;

    auto prepare_deep_target_object_probe_entries(RC::Unreal::UObject* object,
                                                  std::wstring_view source_property)
        -> std::vector<PropertyProbeEntry>;

    auto prepare_tres_equipment_component_offset_probe_data(RC::Unreal::UObject* object,
                                                            bool include_deep_targets)
        -> EquipmentComponentOffsetProbeData;

    auto prepare_live_equipmentish_follow_property_entries(
        RC::Unreal::UObject* followed_object, int max_entries = default_ability_probe_entry_limit)
        -> std::vector<PropertyProbeEntry>;

    auto prepare_live_ability_owner_property_probe_entries(RC::Unreal::UObject* object,
                                                           int max_entries)
        -> std::vector<LiveAbilityOwnerPropertyProbeEntry>;

    auto prepare_live_ability_owner_probe_candidate_data(
        RC::Unreal::UObject* preferred_object, RC::Unreal::UObject* last_world_context_object,
        int max_objects = default_ability_probe_object_limit) -> LiveAbilityOwnerProbeCandidateData;

    auto should_skip_heavy_live_owner_probe_for_reason(std::wstring_view reason) -> bool;

    auto should_log_targeted_sora_state_property(std::wstring_view property_name,
                                                 std::wstring_view property_class) -> bool;

    auto format_targeted_sora_state_property_value(RC::Unreal::UObject* object,
                                                   RC::Unreal::FProperty* property,
                                                   const void* value_ptr, int element_size)
        -> std::wstring;

    auto prepare_targeted_reflected_state_property_entries(RC::Unreal::UObject* object,
                                                           std::wstring_view object_role,
                                                           int max_properties)
        -> std::vector<PropertyProbeEntry>;

    auto prepare_targeted_equipment_reached_asset_map_entries(RC::Unreal::UObject* object,
                                                              std::wstring_view source_property)
        -> std::vector<PropertyProbeEntry>;

    auto prepare_targeted_equipment_manual_offset_probe_data(RC::Unreal::UObject* equipment_object)
        -> EquipmentComponentOffsetProbeData;

    auto prepare_targeted_sora_equipment_link_probe_data(RC::Unreal::UObject* pawn_object)
        -> TargetedSoraEquipmentLinkProbeData;

    auto should_follow_ability_discovery_object_property(std::wstring_view property_name,
                                                         RC::Unreal::UObject* value) -> bool;

    auto collect_ability_discovery_candidates(RC::Unreal::UObject* preferred_object,
                                              RC::Unreal::UObject* last_world_context_object)
        -> std::vector<RC::Unreal::UObject*>;

    auto object_has_ability_discovery_interest(RC::Unreal::UObject* object) -> bool;

    auto should_probe_ability_state_property(std::wstring_view property_name,
                                             std::wstring_view property_class) -> bool;

    auto format_ability_state_property_value(RC::Unreal::UObject* object,
                                             RC::Unreal::FProperty* property, const void* value_ptr,
                                             int element_size) -> std::wstring;

    auto prepare_ability_discovery_snapshot_entries(
        const std::vector<RC::Unreal::UObject*>& candidates,
        int max_objects = default_ability_probe_object_limit,
        int max_properties_per_object = default_ability_probe_property_limit)
        -> std::vector<AbilityDiscoveryObjectProbeEntry>;
} // namespace kh3ap
