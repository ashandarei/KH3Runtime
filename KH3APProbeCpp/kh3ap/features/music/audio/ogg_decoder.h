#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <vector>

namespace kh3ap
{
    // Interleaved signed 16-bit PCM.
    struct DecodedOgg
    {
        uint16_t channels{};
        uint32_t sample_rate{};
        std::vector<int16_t> samples{};
    };

    // A whole Ogg Vorbis file decoded in memory, through stb_vorbis.
    [[nodiscard]] auto decode_ogg_vorbis(const std::vector<std::byte>& bytes,
                                         std::stop_token cancellation = {})
        -> std::optional<DecodedOgg>;
}
