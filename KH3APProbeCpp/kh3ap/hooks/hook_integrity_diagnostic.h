#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace kh3ap
{
    struct RuntimeHookIntegrityDiagnosticDeps
    {
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view events_filename;
        std::wstring_view protocol_name;
        int64_t protocol_version;
        std::wstring_view build_stamp;
        std::wstring_view runtime_context;
    };

    auto process_pending_runtime_hook_integrity_diagnostic(
            const RuntimeHookIntegrityDiagnosticDeps& deps) -> void;
}
