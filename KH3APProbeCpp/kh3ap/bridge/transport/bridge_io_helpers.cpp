#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"

#include "kh3ap/bridge/serialization/bridge_result_serializer.h"
#include "kh3ap/features/grants/delivery/item_apply_serializer.h"
#include "kh3ap/bridge/serialization/request_parser.h"
#include "kh3ap/bridge/utilities.h"

#include <system_error>
#include <fstream>
#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    auto write_text_file_atomic_verified(
            const std::filesystem::path& path,
            std::string_view content) -> bool
    {
        if (pipe::manages(path) && !pipe::durable(path)) return pipe::write(path, content);
        std::error_code ec{};
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                return false;
            }
        }

        std::filesystem::path temp_path{path};
        temp_path += L".tmp";
        try_remove_file(temp_path);
        if (!write_text_file(temp_path, content))
        {
            try_remove_file(temp_path);
            return false;
        }

        const auto temp_text = read_text_file(temp_path);
        if (!temp_text.has_value() || *temp_text != content)
        {
            try_remove_file(temp_path);
            return false;
        }

        if (!MoveFileExW(
                    temp_path.c_str(),
                    path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            try_remove_file(temp_path);
            return false;
        }

        if (pipe::durable(path))
        {
            std::ifstream input(path, std::ios::binary);
            const auto written = read_stream_bounded(input, content.size());
            return written && *written == content && pipe::write(path, content);
        }
        const auto written_text = read_text_file(path);
        return written_text.has_value() && *written_text == content;
    }

    auto try_remove_file(const std::filesystem::path& path) -> void
    {
        if (pipe::manages(path)) { pipe::erase(path); return; }
        std::error_code ec{};
        std::filesystem::remove(path, ec);
    }

    auto get_runtime_item_grant_path(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename) -> std::optional<std::filesystem::path>
    {
        if (!bridge_dir.has_value())
        {
            return std::nullopt;
        }

        return *bridge_dir / std::filesystem::path{filename};
    }

    auto get_runtime_ap_message_path(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename) -> std::optional<std::filesystem::path>
    {
        if (!bridge_dir.has_value())
        {
            return std::nullopt;
        }

        return *bridge_dir / std::filesystem::path{filename};
    }

    auto parse_runtime_ap_message_request_from_text(const std::string& request_text) -> std::optional<RuntimeApMessageRequest>
    {
        return RuntimeRequestParser::parse_ap_message(request_text);
    }

    auto read_runtime_ap_message_request(const std::optional<std::filesystem::path>& request_path) -> std::optional<RuntimeApMessageRequest>
    {
        if (!request_path.has_value())
        {
            return std::nullopt;
        }

        const auto request_text = read_text_file(*request_path);
        if (!request_text.has_value())
        {
            return std::nullopt;
        }

        return parse_runtime_ap_message_request_from_text(*request_text);
    }

    auto write_runtime_ap_message_result(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeApMessageRequest& request,
            std::wstring_view status,
            std::wstring_view error,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool
    {
        if (!bridge_dir.has_value())
        {
            return false;
        }

        const std::string json = RuntimeBridgeResultSerializer::serialize_ap_message_result(
                request,
                status,
                error,
                protocol_name,
                protocol_version);
        return write_text_file(*bridge_dir / std::filesystem::path{filename}, json);
    }

    auto parse_runtime_item_grant_request_from_text(const std::string& request_text) -> std::optional<RuntimeItemGrantRequest>
    {
        return RuntimeRequestParser::parse_item_grant(request_text);
    }

    auto read_runtime_item_grant_request_from_path(const std::filesystem::path& path) -> std::optional<RuntimeItemGrantRequest>
    {
        const auto request_text = read_text_file(path);
        if (!request_text.has_value())
        {
            return std::nullopt;
        }

        return parse_runtime_item_grant_request_from_text(*request_text);
    }

    auto read_runtime_item_grant_request(const std::optional<std::filesystem::path>& request_path) -> std::optional<RuntimeItemGrantRequest>
    {
        if (!request_path.has_value())
        {
            return std::nullopt;
        }

        return read_runtime_item_grant_request_from_path(*request_path);
    }

    auto write_runtime_item_apply_event(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeItemApplyPayload& payload,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool
    {
        if (!bridge_dir.has_value())
        {
            return false;
        }

        const std::string json = RuntimeItemApplySerializer::serialize_event(
                payload,
                protocol_name,
                protocol_version);
        // This acknowledgement is transient and the client already retries a
        // concurrently observed partial JSON document. Avoid temp-file and
        // rename metadata I/O on the game thread.
        return write_text_file(*bridge_dir / std::filesystem::path{filename}, json);
    }

    auto write_runtime_item_apply_batch_event(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeItemApplyBatchPayload& payload,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool
    {
        if (!bridge_dir.has_value())
        {
            return false;
        }

        const std::string json = RuntimeItemApplySerializer::serialize_batch_event(
                payload,
                protocol_name,
                protocol_version);
        // This acknowledgement is transient and the client already retries a
        // concurrently observed partial JSON document. Avoid temp-file and
        // rename metadata I/O on the game thread.
        return write_text_file(*bridge_dir / std::filesystem::path{filename}, json);
    }

    auto write_manual_runtime_item_grant_result(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view filename,
            const RuntimeItemGrantRequest& request,
            std::wstring_view status,
            std::wstring_view error,
            std::wstring_view protocol_name,
            int64_t protocol_version) -> bool
    {
        if (!bridge_dir.has_value())
        {
            return false;
        }

        const std::string json = RuntimeBridgeResultSerializer::serialize_manual_item_grant_result(
                request,
                status,
                error,
                protocol_name,
                protocol_version);
        return write_text_file(*bridge_dir / std::filesystem::path{filename}, json);
    }
}
