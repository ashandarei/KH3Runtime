#pragma once

#include <cstdint>
#include <optional>

namespace kh3ap
{
    enum class RuntimeStyleChangeMode { Shared = 0, PerKeyblade = 1 };

    constexpr auto resolve_runtime_style_change_mode(
            std::optional<int64_t> stylesanity) -> RuntimeStyleChangeMode
    {
        return stylesanity.value_or(1) == 0
                ? RuntimeStyleChangeMode::Shared
                : RuntimeStyleChangeMode::PerKeyblade;
    }
}
