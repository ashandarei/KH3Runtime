#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace kh3ap
{
    // Protocol integers are 64-bit; retail native integer parameters are 32-bit.
    // Reject overflow before a narrowing cast can turn it into a valid command.
    [[nodiscard]] constexpr std::optional<int32_t> checked_native_integer(int64_t value) noexcept
    {
        if (value < std::numeric_limits<int32_t>::min() ||
            value > std::numeric_limits<int32_t>::max())
        {
            return std::nullopt;
        }
        return static_cast<int32_t>(value);
    }
} // namespace kh3ap
