#pragma once

#include "kh3ap/core/types.h"

#include <filesystem>
#include <optional>

namespace kh3ap
{
    class RuntimeContextStore
    {
    public:
        static auto read(const std::filesystem::path& bridge_dir) -> std::optional<RuntimeContextSnapshot>;
        static auto write(const std::filesystem::path& bridge_dir, const RuntimeContextSnapshot& snapshot) -> bool;
    };
}
