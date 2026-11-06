#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    class DiagnosticProbeSerializer
    {
    public:
        static auto serialize_manual_ability_state_probe_result(
                std::wstring_view protocol_name,
                int64_t protocol_version,
                std::wstring_view request_id,
                std::wstring_view status,
                std::wstring_view reason,
                bool include_live_owner,
                std::wstring_view context,
                std::wstring_view build_stamp) -> std::string;

        static auto serialize_manual_equipment_probe_event(
                std::wstring_view build_stamp,
                std::wstring_view kind,
                std::wstring_view request_id,
                std::wstring_view label,
                std::wstring_view stage,
                std::wstring_view context,
                std::wstring_view object_name,
                std::wstring_view class_name,
                std::wstring_view detail) -> std::string;

        static auto serialize_manual_equipment_probe_result(
                std::wstring_view protocol_name,
                int64_t protocol_version,
                std::wstring_view request_id,
                std::wstring_view status,
                std::wstring_view stage,
                std::wstring_view detail,
                std::wstring_view context,
                std::wstring_view build_stamp) -> std::string;

        static auto serialize_property_probe_event(
                std::wstring_view build_stamp,
                std::wstring_view kind,
                std::wstring_view reason,
                std::wstring_view context,
                std::wstring_view object_name,
                std::wstring_view class_name,
                std::wstring_view property_name,
                std::wstring_view property_class,
                int offset,
                int element_size,
                std::wstring_view value) -> std::string;

        static auto serialize_runtime_blocking_ui_state(
                bool active,
                std::wstring_view reason,
                std::wstring_view ui_context,
                std::wstring_view event_name,
                int64_t callback_id) -> std::string;

        static auto serialize_ability_array_needle_probe_event(
                std::wstring_view build_stamp,
                std::wstring_view kind,
                std::wstring_view reason,
                std::wstring_view context,
                std::wstring_view object_name,
                std::wstring_view class_name,
                std::wstring_view property_name,
                std::wstring_view property_class,
                std::wstring_view inner_class,
                int offset,
                int element_size,
                int inner_size,
                int array_num,
                int array_max,
                std::wstring_view matches,
                std::wstring_view value) -> std::string;

    };
}
