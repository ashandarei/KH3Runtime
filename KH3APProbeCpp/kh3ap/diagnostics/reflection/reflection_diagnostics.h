#pragma once

#include "kh3ap/diagnostics/ability_probe/ability_reflection_models.h"
#include "kh3ap/diagnostics/reflection/reflection_diagnostic_models.h"

#include <cstddef>
#include <cstdint>
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
}

namespace kh3ap
{
    class DiagnosticOnceSet;
    class RuntimeState;

    auto dump_function_signature_once(
            std::wstring_view function_path,
            std::wstring_view label,
            bool enabled,
            DiagnosticOnceSet& seen_paths) -> void;

    auto dump_function_signature_for_debug(
            RC::Unreal::UFunction* function_object,
            std::wstring_view label) -> void;

    auto dump_runtime_grant_target_signatures_once(
            bool startup_dump_enabled,
            bool runtime_grant_signature_dump_enabled,
            DiagnosticOnceSet& runtime_grant_signature_paths,
            std::initializer_list<std::pair<std::wstring_view, std::wstring_view>> targets) -> void;

    auto dump_ap_message_candidate_signatures_once(
            bool startup_dump_enabled,
            DiagnosticOnceSet& ap_message_signature_candidates,
            std::initializer_list<std::pair<std::wstring_view, std::wstring_view>> candidates) -> void;

    auto initialize_runtime_startup_diagnostics(
            RuntimeState& runtime_state,
            bool startup_dump_enabled,
            bool runtime_grant_signature_dump_enabled,
            std::wstring_view tres_get_vbonus_path,
            std::wstring_view bp_get_vbonus_path,
            std::wstring_view bp_ability_add_path,
            std::wstring_view bp_ability_get_level_path,
            std::wstring_view bp_is_ability_enable_path,
            std::wstring_view kismet_print_string_path,
            std::wstring_view tresui_open_information_path,
            std::wstring_view tresui_open_center_information_path,
            std::wstring_view tresui_open_message_path,
            std::wstring_view tresui_map_in_notice_message_path,
            std::wstring_view bp_get_item_path,
            std::wstring_view wait_bp_get_multiitem_path,
            std::wstring_view remy_final_milestone_reward_path,
            std::wstring_view tresui_show_get_treasure_prize_snow_curling_path,
            std::wstring_view tresui_show_get_bonus_point_snow_curling_path,
            std::wstring_view uip_map_name_on_callback_path,
            std::wstring_view uip_multi_item_get_on_callback_path) -> void;

    auto serialize_property_probe_entry(
            std::wstring_view build_stamp,
            std::wstring_view runtime_context,
            const PropertyProbeEntry& entry) -> std::string;

    auto serialize_runtime_blocking_ui_state_event(
            bool active,
            std::wstring_view reason,
            std::wstring_view ui_context,
            std::wstring_view event_name,
            int64_t callback_id) -> std::string;

    auto serialize_ability_array_needle_probe_entry(
            std::wstring_view build_stamp,
            std::wstring_view runtime_context,
            std::wstring_view reason,
            const AbilityArrayNeedleProbeEntry& entry) -> std::string;

    auto should_log_rewardish_property(std::wstring_view property_name, std::wstring_view property_class) -> bool;

    auto should_track_event_array_diffs(std::wstring_view property_name) -> bool;

    auto collect_vbonus_level_actor_snapshot() -> std::vector<VBonusLevelActorSnapshotEntry>;
}
