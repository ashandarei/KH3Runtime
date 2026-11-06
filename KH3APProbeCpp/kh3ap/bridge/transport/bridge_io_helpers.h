#pragma once

#include "kh3ap/core/types.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    auto try_remove_file(const std::filesystem::path& path) -> void;
    auto write_text_file_atomic_verified(
            const std::filesystem::path& path,
            std::string_view content) -> bool;
    auto get_runtime_item_grant_path(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename) -> std::optional<std::filesystem::path>;
    auto get_runtime_ap_message_path(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename) -> std::optional<std::filesystem::path>;
    auto parse_runtime_ap_message_request_from_text(const std::string& request_text) -> std::optional<RuntimeApMessageRequest>;
    auto read_runtime_ap_message_request(const std::optional<std::filesystem::path>& request_path) -> std::optional<RuntimeApMessageRequest>;
    auto write_runtime_ap_message_result(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeApMessageRequest& request,
            std::wstring_view status,
            std::wstring_view error,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool;
    auto parse_runtime_item_grant_request_from_text(const std::string& request_text) -> std::optional<RuntimeItemGrantRequest>;
    auto read_runtime_item_grant_request_from_path(const std::filesystem::path& path) -> std::optional<RuntimeItemGrantRequest>;
    auto read_runtime_item_grant_request(const std::optional<std::filesystem::path>& request_path) -> std::optional<RuntimeItemGrantRequest>;
    auto write_runtime_item_apply_event(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeItemApplyPayload& payload,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool;
    auto write_runtime_item_apply_batch_event(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeItemApplyBatchPayload& payload,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool;
    auto write_manual_runtime_item_grant_result(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeItemGrantRequest& request,
            std::wstring_view status,
            std::wstring_view error,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool;
}
