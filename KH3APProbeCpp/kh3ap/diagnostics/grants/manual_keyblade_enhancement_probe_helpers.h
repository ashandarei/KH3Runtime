#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace kh3ap
{
    struct ManualKeybladeEnhancementProbeDeps
    {
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view events_filename;
        std::wstring_view protocol_name;
        int64_t protocol_version;
        std::wstring_view runtime_context_summary;
        std::wstring_view build_stamp;
        std::wstring_view data_table_path;
        std::wstring_view item_manager_path;
        std::wstring_view checker_default_object_path;
        std::wstring_view enhance_test_function_path;
    };

    auto make_manual_keyblade_enhancement_probe_deps(
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view request_filename,
            std::wstring_view result_filename,
            std::wstring_view events_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view runtime_context_summary,
            std::wstring_view build_stamp,
            std::wstring_view data_table_path,
            std::wstring_view item_manager_path,
            std::wstring_view checker_default_object_path,
            std::wstring_view enhance_test_function_path) -> ManualKeybladeEnhancementProbeDeps;

    auto process_pending_manual_keyblade_enhancement_probe(
            const ManualKeybladeEnhancementProbeDeps& deps) -> void;
}
