#pragma once

#include "kh3ap/core/types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    auto try_call_tresui_open_information(
            const RuntimeApMessageRequest& request,
            RuntimeState& runtime_state,
            std::wstring_view function_path,
            std::wstring_view blueprint_library_default_object_path,
            std::wstring* failure_reason = nullptr,
            std::wstring_view clear_information_text_cache_path = {}) -> bool;

    auto try_call_kismet_print_string(
            const RuntimeApMessageRequest& request,
            RC::Unreal::UObject* world_context_object,
            std::wstring_view function_path,
            std::wstring_view default_object_path,
            std::wstring* failure_reason = nullptr) -> bool;

    auto process_pending_runtime_ap_message(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            const std::optional<std::filesystem::path>& request_path,
            bool request_file_exists,
            std::wstring_view result_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view tresui_open_information_path,
            std::wstring_view tresui_clear_information_text_cache_path,
            std::wstring_view tres_game_blueprint_library_default_object_path,
            std::wstring_view kismet_print_string_path,
            std::wstring_view kismet_system_library_default_object_path,
            std::wstring_view runtime_context_summary) -> void;
}
