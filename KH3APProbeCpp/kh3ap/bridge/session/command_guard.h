#pragma once

#include "kh3ap/bridge/serialization/json_fields.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    inline constexpr int64_t runtime_command_max_lifetime_ms = 600000;

    // Every request is bound to the observed process and a short validity window.
    // Files left by an earlier session must never become commands in a new one.
    inline auto runtime_command_request_failure(
            const std::string& text, uint32_t current_pid, int64_t now_ms) -> std::wstring_view
    {
        const auto schema = JsonFieldReader::string_field(text, "request_schema");
        const auto pid = JsonFieldReader::int64_field(text, "process_id");
        const auto expires = JsonFieldReader::int64_field(text, "expires_unix_ms");
        if (!schema || *schema != L"kh3ap.runtime_control.v1")
        {
            return L"unsupported_request_schema";
        }
        if (!pid || *pid != current_pid) return L"process_id_mismatch";
        if (!expires || *expires <= now_ms ||
            static_cast<uint64_t>(*expires) - static_cast<uint64_t>(now_ms) >
                static_cast<uint64_t>(runtime_command_max_lifetime_ms))
        {
            return L"invalid_or_expired_request";
        }
        return {};
    }
}
