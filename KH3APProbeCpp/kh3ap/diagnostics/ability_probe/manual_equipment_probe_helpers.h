#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct ManualEquipmentProbeDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view protocol_name;
        int64_t protocol_version;
        std::wstring_view runtime_context_summary;
        std::wstring_view build_stamp;
    };

    auto make_manual_equipment_probe_deps(
            RuntimeState& runtime_state,
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view request_filename,
            std::wstring_view result_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view runtime_context_summary,
            std::wstring_view build_stamp) -> ManualEquipmentProbeDeps;

    auto process_pending_manual_equipment_probe(const ManualEquipmentProbeDeps& deps) -> void;
}
