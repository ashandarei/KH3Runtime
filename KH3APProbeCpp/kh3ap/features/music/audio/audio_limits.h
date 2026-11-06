#pragma once

#include <cstddef>

namespace kh3ap::audio
{
    inline constexpr size_t max_file_bytes = 256 * 1024 * 1024;
    inline constexpr size_t max_sample_bytes = 256 * 1024 * 1024;
    inline constexpr size_t max_decoder_bytes = 32 * 1024 * 1024;
    inline constexpr unsigned max_channels = 8;
    inline constexpr unsigned max_sample_rate = 192000;
} // namespace kh3ap::audio
