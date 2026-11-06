#pragma once

namespace kh3ap::ability_probe_limits
{
    // Bounds and output budgets for immediate diagnostic reads; values preserve existing behavior.
    inline constexpr auto max_other_owner_properties = 24;
    inline constexpr auto max_nested_needle_properties = 96;
    inline constexpr auto max_needle_matches_per_object = 24;
    inline constexpr auto max_needle_matches = 96;
    inline constexpr auto max_needle_objects = 48;
    inline constexpr auto object_name_log_chars = 160;
    inline constexpr auto scalar_value_log_chars = 220;
    inline constexpr auto targeted_value_log_chars = 520;
    inline constexpr auto array_value_log_chars = 720;
    inline constexpr auto deep_value_log_chars = 420;
    inline constexpr auto candidate_name_log_chars = 120;
    inline constexpr auto max_live_owner_array_entries = 256;
    inline constexpr auto max_scalar_bytes = 8;
    inline constexpr auto thunder_up_ability_id = 0x23;
    inline constexpr auto max_live_owner_preview_entries = 24;
    inline constexpr auto live_owner_preview_bytes = 32;
    inline constexpr auto max_needle_array_entries = 128;
    inline constexpr auto max_needle_preview_entries = 32;
    inline constexpr auto needle_preview_bytes = 64;
    inline constexpr auto max_scalar_map_entries = 96;
    inline constexpr auto max_scalar_map_stride = 40;
    inline constexpr auto max_scalar_map_preview_entries = 12;
    inline constexpr auto scalar_map_entry_preview_bytes = 16;
    inline constexpr auto map_alignment_mask = 7U;
    inline constexpr auto map_entry_metadata_bytes = 8;
    inline constexpr auto max_fallback_class_candidates = 8;
    inline constexpr auto max_initial_context_candidates = 12;
    inline constexpr auto max_direct_owner_candidates = 48;
    inline constexpr auto max_followed_owner_candidates = 64;
    inline constexpr auto max_followed_properties = 8;
    inline constexpr auto max_deep_target_properties = 48;
    inline constexpr auto deep_parameter_chunk_count = 8;
    inline constexpr auto targeted_parameter_chunk_count = 4;
    inline constexpr auto parameter_chunk_bytes = 32;
    inline constexpr auto raw_map_log_chars = 1200;
    inline constexpr auto nested_map_log_chars = 2200;
    inline constexpr auto max_manual_array_entries = 64;
    inline constexpr auto max_manual_array_element_bytes = 64;
    inline constexpr auto max_manual_preview_entries = 12;
    inline constexpr auto manual_array_entry_preview_bytes = 24;
    inline constexpr auto max_manual_map_entries = 128;
    inline constexpr auto max_manual_map_entry_bytes = 256;
    inline constexpr auto manual_map_entry_preview_bytes = 48;
    inline constexpr auto max_nested_array_fields = 8;
    inline constexpr auto max_nested_enum_entries = 256;
    inline constexpr auto max_nested_enum_preview_entries = 32;
    inline constexpr auto nested_enum_preview_bytes = 96;
    inline constexpr auto max_nested_map_value_bytes = 256;
    inline constexpr auto max_nested_map_stride = 320;
    inline constexpr auto max_nested_map_preview_entries = 16;
    inline constexpr auto max_equipment_owner_properties = 80;
    inline constexpr auto state_property_preview_bytes = 16;
    inline constexpr auto state_struct_preview_bytes = 32;
    inline constexpr auto followed_value_log_chars = 320;
}
