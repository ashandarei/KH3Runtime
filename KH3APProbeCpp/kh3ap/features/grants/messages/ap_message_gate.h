#pragma once

#include "kh3ap/app/state.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    struct RuntimeApMessageGateState
    {
        std::chrono::steady_clock::time_point quiet_until{};
        std::wstring quiet_source{};
        std::wstring last_defer_reason{};
    };

    auto extend_runtime_ap_message_quiet_window(
            RuntimeApMessageGateState& state,
            std::wstring_view source,
            std::chrono::milliseconds quiet_window) -> void;

    auto runtime_ap_message_defer_reason(
            RuntimeApMessageGateState& state,
            RuntimeState& runtime_state,
            bool pending_request_file_exists) -> std::wstring;

    auto maybe_log_runtime_ap_message_deferred(
            RuntimeApMessageGateState& state,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view reason,
            std::wstring_view build_stamp,
            std::wstring_view runtime_context) -> void;
}
