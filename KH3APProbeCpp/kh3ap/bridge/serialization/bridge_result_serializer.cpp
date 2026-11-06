#include "kh3ap/bridge/serialization/bridge_result_serializer.h"

#include "kh3ap/bridge/utilities.h"

#include <sstream>

namespace kh3ap
{
    auto RuntimeBridgeResultSerializer::serialize_ap_message_result(
            const RuntimeApMessageRequest& request,
            std::wstring_view status,
            std::wstring_view error,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> std::string
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << protocol_version << ",\n";
        json << "  \"request_id\": \"" << json_escape(request.request_id) << "\",\n";
        json << "  \"message\": \"" << json_escape(request.message) << "\",\n";
        json << "  \"status\": \"" << json_escape(status) << "\",\n";
        json << "  \"processed_at\": \"" << now_iso8601_utc() << "\"";
        if (!error.empty())
        {
            json << ",\n  \"error\": \"" << json_escape(error) << "\"";
        }
        json << "\n}\n";
        return json.str();
    }

    auto RuntimeBridgeResultSerializer::serialize_manual_item_grant_result(
            const RuntimeItemGrantRequest& request,
            std::wstring_view status,
            std::wstring_view error,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> std::string
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << protocol_version << ",\n";
        json << "  \"request_id\": \"" << json_escape(request.request_id) << "\",\n";
        json << "  \"index\": " << request.index << ",\n";
        json << "  \"item_name\": \"" << json_escape(request.item_name) << "\",\n";
        json << "  \"kh3_runtime_token\": \"" << json_escape(request.runtime_token) << "\",\n";
        json << "  \"kh3_grant_method\": \"" << json_escape(request.grant_method) << "\",\n";
        json << "  \"status\": \"" << json_escape(status) << "\",\n";
        json << "  \"processed_at\": \"" << now_iso8601_utc() << "\"";
        if (!error.empty())
        {
            json << ",\n  \"error\": \"" << json_escape(error) << "\"";
        }
        json << "\n}\n";
        return json.str();
    }
}
