#include "kh3ap/diagnostics/output/diagnostic_probe_serializer.h"

#include "kh3ap/bridge/utilities.h"

#include <sstream>

namespace kh3ap
{
    auto DiagnosticProbeSerializer::serialize_manual_ability_state_probe_result(
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view request_id,
            std::wstring_view status,
            std::wstring_view reason,
            bool include_live_owner,
            std::wstring_view context,
            std::wstring_view build_stamp) -> std::string
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << protocol_version << ",\n";
        json << "  \"request_id\": \"" << json_escape(request_id) << "\",\n";
        json << "  \"status\": \"" << json_escape(status) << "\",\n";
        json << "  \"reason\": \"" << json_escape(reason) << "\",\n";
        json << "  \"include_live_owner\": " << (include_live_owner ? "true" : "false") << ",\n";
        json << "  \"context\": \"" << json_escape(context) << "\",\n";
        json << "  \"build_stamp\": \"" << json_escape(build_stamp) << "\",\n";
        json << "  \"ability_array_log\": \"ability_array_needle_probe.jsonl\",\n";
        json << "  \"targeted_sora_state_log\": \"targeted_sora_state_probe.jsonl\",\n";
        json << "  \"live_owner_log\": \"live_ability_owner_probe.jsonl\"\n";
        json << "}\n";
        return json.str();
    }

    auto DiagnosticProbeSerializer::serialize_manual_equipment_probe_event(
            std::wstring_view build_stamp,
            std::wstring_view kind,
            std::wstring_view request_id,
            std::wstring_view label,
            std::wstring_view stage,
            std::wstring_view context,
            std::wstring_view object_name,
            std::wstring_view class_name,
            std::wstring_view detail) -> std::string
    {
        std::ostringstream json;
        json << "{"
             << "\"at\":\"" << now_iso8601_utc() << "\","
             << "\"build_stamp\":\"" << json_escape(build_stamp) << "\","
             << "\"kind\":\"" << json_escape(kind) << "\","
             << "\"request_id\":\"" << json_escape(request_id) << "\","
             << "\"label\":\"" << json_escape(label) << "\","
             << "\"stage\":\"" << json_escape(stage) << "\","
             << "\"context\":\"" << json_escape(context) << "\","
             << "\"object\":\"" << json_escape(object_name) << "\","
             << "\"class\":\"" << json_escape(class_name) << "\","
             << "\"detail\":\"" << json_escape(detail) << "\""
             << "}\n";
        return json.str();
    }

    auto DiagnosticProbeSerializer::serialize_manual_equipment_probe_result(
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view request_id,
            std::wstring_view status,
            std::wstring_view stage,
            std::wstring_view detail,
            std::wstring_view context,
            std::wstring_view build_stamp) -> std::string
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << protocol_version << ",\n";
        json << "  \"request_id\": \"" << json_escape(request_id) << "\",\n";
        json << "  \"status\": \"" << json_escape(status) << "\",\n";
        json << "  \"stage\": \"" << json_escape(stage) << "\",\n";
        json << "  \"detail\": \"" << json_escape(detail) << "\",\n";
        json << "  \"context\": \"" << json_escape(context) << "\",\n";
        json << "  \"build_stamp\": \"" << json_escape(build_stamp) << "\",\n";
        json << "  \"log\": \"manual_equipment_probe.jsonl\"\n";
        json << "}\n";
        return json.str();
    }

    auto DiagnosticProbeSerializer::serialize_property_probe_event(
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
            std::wstring_view value) -> std::string
    {
        std::ostringstream json;
        json << "{"
             << "\"at\":\"" << now_iso8601_utc() << "\","
             << "\"build_stamp\":\"" << json_escape(build_stamp) << "\","
             << "\"kind\":\"" << json_escape(kind) << "\","
             << "\"reason\":\"" << json_escape(reason) << "\","
             << "\"context\":\"" << json_escape(context) << "\","
             << "\"object\":\"" << json_escape(object_name) << "\","
             << "\"class\":\"" << json_escape(class_name) << "\"";
        if (!property_name.empty())
        {
            json << ",\"property\":\"" << json_escape(property_name) << "\""
                 << ",\"property_class\":\"" << json_escape(property_class) << "\""
                 << ",\"offset\":" << offset
                 << ",\"element_size\":" << element_size
                 << ",\"value\":\"" << json_escape(value) << "\"";
        }
        json << "}\n";
        return json.str();
    }

    auto DiagnosticProbeSerializer::serialize_runtime_blocking_ui_state(
            bool active,
            std::wstring_view reason,
            std::wstring_view ui_context,
            std::wstring_view event_name,
            int64_t callback_id) -> std::string
    {
        std::ostringstream json;
        json << "{\n"
             << "  \"blocking_ui_active\": " << (active ? "true" : "false") << ",\n"
             << "  \"reason\": \"" << json_escape(reason) << "\",\n"
             << "  \"ui_context\": \"" << json_escape(ui_context) << "\",\n"
             << "  \"event\": \"" << json_escape(event_name) << "\",\n"
             << "  \"callback_id\": " << callback_id << ",\n"
             << "  \"updated_at\": \"" << now_iso8601_utc() << "\"\n"
             << "}\n";
        return json.str();
    }

    auto DiagnosticProbeSerializer::serialize_ability_array_needle_probe_event(
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
            std::wstring_view value) -> std::string
    {
        std::ostringstream json;
        json << "{"
             << "\"at\":\"" << now_iso8601_utc() << "\","
             << "\"build_stamp\":\"" << json_escape(build_stamp) << "\","
             << "\"kind\":\"" << json_escape(kind) << "\","
             << "\"reason\":\"" << json_escape(reason) << "\","
             << "\"context\":\"" << json_escape(context) << "\","
             << "\"object\":\"" << json_escape(object_name) << "\","
             << "\"class\":\"" << json_escape(class_name) << "\"";
        if (!property_name.empty())
        {
            json << ",\"property\":\"" << json_escape(property_name) << "\""
                 << ",\"property_class\":\"" << json_escape(property_class) << "\""
                 << ",\"inner_class\":\"" << json_escape(inner_class) << "\""
                 << ",\"offset\":" << offset
                 << ",\"element_size\":" << element_size
                 << ",\"inner_size\":" << inner_size
                 << ",\"array_num\":" << array_num
                 << ",\"array_max\":" << array_max
                 << ",\"matches\":\"" << json_escape(matches) << "\""
                 << ",\"value\":\"" << json_escape(value) << "\"";
        }
        json << "}\n";
        return json.str();
    }


}
