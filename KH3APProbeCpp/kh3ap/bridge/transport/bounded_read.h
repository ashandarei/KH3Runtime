#pragma once
#include <algorithm>
#include <array>
#include <istream>
#include <optional>
#include <string>

namespace kh3ap
{
    // Enforce the budget on the open stream, including files that grow after
    // a size check. Never return a truncated document as a successful read.
    inline auto read_stream_bounded(std::istream& input, size_t limit) -> std::optional<std::string>
    {
        std::string result;
        std::array<char, 4096> buffer{};
        while (result.size() < limit)
        {
            const auto count = std::min(buffer.size(), limit - result.size());
            input.read(buffer.data(), static_cast<std::streamsize>(count));
            if (input.bad() || (input.fail() && !input.eof()))
            {
                return std::nullopt;
            }
            result.append(buffer.data(), static_cast<size_t>(input.gcount()));
            if (input.eof())
            {
                return result;
            }
        }
        if (input.peek() != std::char_traits<char>::eof() || !input.eof() || input.bad())
        {
            return std::nullopt;
        }
        return result;
    }
} // namespace kh3ap
