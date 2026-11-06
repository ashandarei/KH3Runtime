#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeNearbyActorScanDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view events_filename;
        std::wstring_view protocol_name;
        int protocol_version{};
        std::wstring_view build_stamp;
        std::wstring_view actor_location_function_path;
        bool debug_logging{};
    };

    auto process_pending_runtime_nearby_actor_scan(const RuntimeNearbyActorScanDeps& deps) -> void;
}
