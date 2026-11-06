#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeSoraPositionProbeState
    {
        std::chrono::steady_clock::time_point last_sample_at{};
    };

    struct RuntimeSoraPositionProbeDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view actor_location_function_path;
        std::wstring_view output_filename;
        std::wstring_view protocol_name;
        int protocol_version{};
        std::wstring_view build_stamp;
        std::chrono::steady_clock::duration stable_delay{};
        std::chrono::steady_clock::duration sample_interval{};
        bool debug_logging{};
    };

    auto maybe_append_runtime_sora_position_probe(
            RuntimeSoraPositionProbeState& state,
            const RuntimeSoraPositionProbeDeps& deps,
            std::chrono::steady_clock::time_point now) -> void;
}
