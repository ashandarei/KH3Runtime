#include "kh3ap/features/grants/delivery/item_apply_serializer.h"

#include "kh3ap/bridge/utilities.h"

#include <sstream>

namespace kh3ap
{
    auto RuntimeItemApplySerializer::serialize_event(
            const RuntimeItemApplyPayload& payload,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> std::string
    {
        const uint64_t applied_filetime_100ns = current_windows_filetime_100ns();
        const std::string applied_at = format_windows_filetime_utc(applied_filetime_100ns);
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << protocol_version << ",\n";
        if (!payload.request_id.empty())
        {
            json << "  \"request_id\": \"" << json_escape(payload.request_id) << "\",\n";
        }
        if (!payload.action_kind.empty())
        {
            json << "  \"action_kind\": \"" << json_escape(payload.action_kind) << "\",\n";
        }
        json << "  \"index\": " << payload.index << ",\n";
        json << "  \"item_name\": \"" << json_escape(payload.item_name) << "\",\n";
        json << "  \"kh3_runtime_token\": \"" << json_escape(payload.runtime_token) << "\",\n";
        json << "  \"kh3_grant_method\": \"" << json_escape(payload.grant_method) << "\",\n";
        json << "  \"status\": \"" << json_escape(payload.status) << "\",\n";
        json << "  \"applied_at\": \"" << applied_at << "\",\n";
        json << "  \"applied_filetime_100ns\": " << applied_filetime_100ns << ",\n";
        json << "  \"timestamp_semantics\": \"post_grant_serialization_time\"";
        if (payload.runtime_request_wait_ms >= 0)
        {
            json << ",\n  \"runtime_request_wait_ms\": " << payload.runtime_request_wait_ms;
        }
        if (payload.runtime_grant_apply_elapsed_ms >= 0)
        {
            json << ",\n  \"runtime_grant_apply_elapsed_ms\": " << payload.runtime_grant_apply_elapsed_ms;
        }
        if (!payload.error.empty())
        {
            json << ",\n  \"error\": \"" << json_escape(payload.error) << "\"";
        }
        json << "\n}\n";
        return json.str();
    }

    auto RuntimeItemApplySerializer::serialize_batch_event(
            const RuntimeItemApplyBatchPayload& payload,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> std::string
    {
        const uint64_t applied_filetime_100ns = current_windows_filetime_100ns();
        const std::string applied_at = format_windows_filetime_utc(applied_filetime_100ns);
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << protocol_version << ",\n";
        if (!payload.request_id.empty())
        {
            json << "  \"request_id\": \"" << json_escape(payload.request_id) << "\",\n";
        }
        if (!payload.action_kind.empty())
        {
            json << "  \"action_kind\": \"" << json_escape(payload.action_kind) << "\",\n";
        }
        json << "  \"status\": \"" << json_escape(payload.status) << "\",\n";
        json << "  \"applied_at\": \"" << applied_at << "\",\n";
        json << "  \"applied_filetime_100ns\": " << applied_filetime_100ns << ",\n";
        json << "  \"timestamp_semantics\": \"post_grant_serialization_time\",\n";
        if (payload.runtime_request_wait_ms >= 0)
        {
            json << "  \"runtime_request_wait_ms\": " << payload.runtime_request_wait_ms << ",\n";
        }
        if (payload.runtime_grant_apply_elapsed_ms >= 0)
        {
            json << "  \"runtime_grant_apply_elapsed_ms\": " << payload.runtime_grant_apply_elapsed_ms << ",\n";
        }
        json << "  \"request_complete\": " << (payload.request_complete ? "true" : "false") << ",\n";
        json << "  \"owner\": \"dll\",\n";
        json << "  \"process_id\": " << payload.process_id << ",\n";
        json << "  \"delivery_epoch\": " << payload.delivery_epoch << ",\n";
        json << "  \"seed_name\": \"" << json_escape(payload.seed_name) << "\",\n";
        json << "  \"slot\": " << payload.slot << ",\n";
        json << "  \"applied_indices\": [";
        for (size_t index = 0; index < payload.applied.size(); ++index)
        {
            if (index > 0)
            {
                json << ", ";
            }
            json << payload.applied[index].index;
        }
        json << "],\n";
        json << "  \"applied_grants\": [\n";
        for (size_t index = 0; index < payload.applied.size(); ++index)
        {
            const auto& grant = payload.applied[index];
            json << "    {\n";
            json << "      \"index\": " << grant.index << ",\n";
            json << "      \"item_name\": \"" << json_escape(grant.item_name) << "\",\n";
            json << "      \"kh3_runtime_token\": \"" << json_escape(grant.runtime_token) << "\",\n";
            json << "      \"kh3_grant_method\": \"" << json_escape(grant.grant_method) << "\",\n";
            json << "      \"status\": \"" << json_escape(grant.status) << "\"";
            if (!grant.error.empty())
            {
                json << ",\n      \"error\": \"" << json_escape(grant.error) << "\"";
            }
            json << "\n    }";
            if (index + 1 < payload.applied.size())
            {
                json << ",";
            }
            json << "\n";
        }
        json << "  ]";
        if (!payload.error.empty())
        {
            json << ",\n  \"error\": \"" << json_escape(payload.error) << "\"";
        }
        json << "\n}\n";
        return json.str();
    }
}
