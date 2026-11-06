#include <optional>
#include <utility>
#include "kh3ap/bridge/serialization/request_parser.h"

#include "kh3ap/bridge/serialization/json_fields.h"

#include <algorithm>

namespace kh3ap
{
    namespace
    {
        constexpr double min_ap_message_duration_seconds = 1.0;
        constexpr double max_ap_message_duration_seconds = 15.0;
        constexpr auto max_request_objects = 4096;
    } // namespace

    auto RuntimeRequestParser::parse_ap_message(const std::string& request_text)
        -> std::optional<RuntimeApMessageRequest>
    {
        RuntimeApMessageRequest request{};
        const auto message = JsonFieldReader::string_field(request_text, "message");
        if (!message.has_value() || message->empty())
        {
            return std::nullopt;
        }

        request.message = *message;
        if (const auto request_id = JsonFieldReader::string_field(request_text, "request_id");
            request_id.has_value())
        {
            request.request_id = *request_id;
        }
        if (const auto bridge_key = JsonFieldReader::string_field(request_text, "bridge_key");
            bridge_key.has_value())
        {
            request.bridge_key = *bridge_key;
        }
        if (const auto location_name = JsonFieldReader::string_field(request_text, "location_name");
            location_name.has_value())
        {
            request.location_name = *location_name;
        }
        if (const auto item_name = JsonFieldReader::string_field(request_text, "item_name");
            item_name.has_value())
        {
            request.item_name = *item_name;
        }
        if (const auto duration_seconds =
                JsonFieldReader::number_field(request_text, "duration_seconds");
            duration_seconds.has_value())
        {
            request.duration_seconds =
                std::clamp(*duration_seconds, min_ap_message_duration_seconds,
                           max_ap_message_duration_seconds);
        }
        return request;
    }

    auto RuntimeRequestParser::parse_item_grant(const std::string& request_text)
        -> std::optional<RuntimeItemGrantRequest>
    {
        RuntimeItemGrantRequest request{};
        const auto index = JsonFieldReader::int64_field(request_text, "index");
        const auto runtime_token = JsonFieldReader::string_field(request_text, "kh3_runtime_token");
        const auto grant_method = JsonFieldReader::string_field(request_text, "kh3_grant_method");
        if (!index.has_value() || !runtime_token.has_value() || !grant_method.has_value())
        {
            return std::nullopt;
        }

        request.index = *index;
        request.runtime_token = *runtime_token;
        request.grant_method = *grant_method;
        if (const auto runtime_flag_name =
                JsonFieldReader::string_field(request_text, "kh3_runtime_flag_name");
            runtime_flag_name.has_value())
        {
            request.runtime_flag_name = *runtime_flag_name;
        }
        if (const auto runtime_vbonus_source_id =
                JsonFieldReader::string_field(request_text, "kh3_runtime_vbonus_source_id");
            runtime_vbonus_source_id.has_value())
        {
            request.runtime_vbonus_source_id = *runtime_vbonus_source_id;
        }
        if (const auto request_id = JsonFieldReader::string_field(request_text, "request_id");
            request_id.has_value())
        {
            request.request_id = *request_id;
        }
        if (const auto item_name = JsonFieldReader::string_field(request_text, "item_name");
            item_name.has_value())
        {
            request.item_name = *item_name;
        }
        if (const auto from_player_name =
                JsonFieldReader::string_field(request_text, "from_player_name");
            from_player_name.has_value())
        {
            request.from_player_name = *from_player_name;
        }
        if (const auto location_name = JsonFieldReader::string_field(request_text, "location_name");
            location_name.has_value())
        {
            request.location_name = *location_name;
        }
        if (const auto action_kind = JsonFieldReader::string_field(request_text, "action_kind");
            action_kind.has_value())
        {
            request.action_kind = *action_kind;
        }
        request.randomized_tokens =
            JsonFieldReader::string_array_field(request_text, "kh3_randomized_tokens");
        request.bundle_display =
            JsonFieldReader::string_array_field(request_text, "kh3_bundle_display");
        request.vanilla_tokens =
            JsonFieldReader::string_array_field(request_text, "kh3_vanilla_tokens");
        if (const auto randomized_summary =
                JsonFieldReader::string_field(request_text, "kh3_randomized_summary");
            randomized_summary.has_value())
        {
            request.randomized_summary = *randomized_summary;
        }
        if (const auto vanilla_summary =
                JsonFieldReader::string_field(request_text, "kh3_vanilla_summary");
            vanilla_summary.has_value())
        {
            request.vanilla_summary = *vanilla_summary;
        }
        if (const auto event_gameflow_flag =
                JsonFieldReader::string_field(request_text, "kh3_event_gameflow_flag");
            event_gameflow_flag.has_value())
        {
            request.event_gameflow_flag = *event_gameflow_flag;
        }
        if (const auto event_gameflow_source =
                JsonFieldReader::string_field(request_text, "kh3_event_gameflow_source");
            event_gameflow_source.has_value())
        {
            request.event_gameflow_source = *event_gameflow_source;
        }
        if (const auto event_original_item =
                JsonFieldReader::string_field(request_text, "kh3_event_original_item");
            event_original_item.has_value())
        {
            request.event_original_item = *event_original_item;
        }
        if (const auto munny_delta = JsonFieldReader::int64_field(request_text, "kh3_munny_delta");
            munny_delta.has_value())
        {
            request.munny_delta = *munny_delta;
        }
        if (const auto show_info = JsonFieldReader::bool_field(request_text, "show_info");
            show_info.has_value())
        {
            request.show_info = *show_info;
        }
        if (const auto reapply_context_key =
                JsonFieldReader::string_field(request_text, "reapply_context_key");
            reapply_context_key.has_value())
        {
            request.reapply_context_key = *reapply_context_key;
        }
        return request;
    }

    auto RuntimeRequestParser::is_supported_batch_action(const std::wstring& action_kind) -> bool
    {
        return action_kind == L"runtime_item_grant_batch" || action_kind == L"ap_replay_batch" ||
               action_kind == L"save_load_replay_batch";
    }

    auto RuntimeRequestParser::parse_item_grant_batch(const std::string& request_text)
        -> std::optional<RuntimeItemGrantBatchRequest>
    {
        const auto action_kind = JsonFieldReader::string_field(request_text, "action_kind");
        if (!action_kind.has_value() || (!is_supported_batch_action(*action_kind) &&
                                         *action_kind != L"deferred_bad_status_trap"))
        {
            return std::nullopt;
        }

        RuntimeItemGrantBatchRequest batch{};
        batch.request_schema =
            JsonFieldReader::string_field(request_text, "request_schema").value_or(L"");
        batch.process_id = JsonFieldReader::int64_field(request_text, "process_id").value_or(0);
        batch.delivery_epoch =
            JsonFieldReader::int64_field(request_text, "delivery_epoch").value_or(-1);
        batch.seed_name = JsonFieldReader::string_field(request_text, "seed_name").value_or(L"");
        batch.slot = JsonFieldReader::int64_field(request_text, "slot").value_or(0);
        batch.action_kind = *action_kind;
        batch.expand_catalog =
            JsonFieldReader::bool_field(request_text, "expand_catalog").value_or(false);
        if (const auto request_id = JsonFieldReader::string_field(request_text, "request_id");
            request_id.has_value())
        {
            batch.request_id = *request_id;
        }
        if (const auto reapply_context_key =
                JsonFieldReader::string_field(request_text, "reapply_context_key");
            reapply_context_key.has_value())
        {
            batch.reapply_context_key = *reapply_context_key;
        }

        const auto objects = JsonFieldReader::object_array_field(request_text, "grants");
        if (objects.empty() || objects.size() > max_request_objects)
        {
            return std::nullopt;
        }
        for (const std::string& object_text : objects)
        {
            std::optional<RuntimeItemGrantRequest> grant;
            if (batch.expand_catalog)
            {
                const auto index = JsonFieldReader::int64_field(object_text, "index");
                if (!index || *index < 0)
                {
                    return std::nullopt;
                }
                grant = RuntimeItemGrantRequest{.index = *index};
                grant->show_info =
                    JsonFieldReader::bool_field(object_text, "show_info").value_or(false);
            }
            else
            {
                grant = parse_item_grant(object_text);
            }
            if (!grant.has_value())
            {
                // Dropping a malformed secondary action could acknowledge
                // its primary receipt without delivering the whole reward.
                return std::nullopt;
            }
            if (grant->request_id.empty())
            {
                grant->request_id = batch.request_id;
            }
            if (grant->action_kind.empty())
            {
                grant->action_kind = batch.action_kind;
            }
            batch.grants.emplace_back(std::move(*grant));
        }

        if (batch.grants.empty())
        {
            return std::nullopt;
        }
        return batch;
    }
} // namespace kh3ap
