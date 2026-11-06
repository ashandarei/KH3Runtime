#pragma once

#include "kh3ap/core/types.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace kh3ap
{
    class RuntimeLootPayloadStore
    {
    public:
        static auto write(
                const std::vector<std::filesystem::path>& output_paths,
                const RuntimeLootPayload& payload) -> bool;

        static auto emit_once(
                const std::vector<std::filesystem::path>& output_paths,
                const RuntimeLootPayload& payload) -> bool;

        static auto should_emit(
                const RuntimeLootPayload& payload,
                const std::optional<RuntimeContextSnapshot>& runtime_context,
                bool debug_logging) -> bool;
    };
}
