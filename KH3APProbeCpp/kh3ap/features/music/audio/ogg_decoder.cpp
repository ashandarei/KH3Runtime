#include "kh3ap/features/music/audio/ogg_decoder.h"
#include "kh3ap/features/music/audio/audio_limits.h"

#include <array>
#include <cstring>
#include <limits>
#include <memory>

// The implementation is compiled once in the separate kh3ap_vorbis target.
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

namespace kh3ap
{
    auto decode_ogg_vorbis(const std::vector<std::byte>& bytes, std::stop_token cancellation)
        -> std::optional<DecodedOgg>
    {
        constexpr size_t ogg_magic_bytes = 4;
        if (cancellation.stop_requested() || bytes.size() < ogg_magic_bytes ||
            bytes.size() > audio::max_file_bytes ||
            bytes.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            return std::nullopt;
        }
        if (std::memcmp(bytes.data(), "OggS", ogg_magic_bytes) != 0)
        {
            return std::nullopt;
        }
        // The decoder's setup and scratch allocations share a fixed arena.
        // Crafted codebooks/comments cannot request an unbounded heap allocation
        // before stream metadata is available to this wrapper.
        const auto workspace = std::make_unique_for_overwrite<char[]>(audio::max_decoder_bytes);
        const stb_vorbis_alloc allocation{workspace.get(),
                                          static_cast<int>(audio::max_decoder_bytes)};
        int error{};
        const std::unique_ptr<stb_vorbis, decltype(&stb_vorbis_close)> decoder{
            stb_vorbis_open_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                   static_cast<int>(bytes.size()), &error, &allocation),
            &stb_vorbis_close};
        if (!decoder)
        {
            return std::nullopt;
        }
        const auto info = stb_vorbis_get_info(decoder.get());
        if (info.channels <= 0 || static_cast<unsigned>(info.channels) > audio::max_channels ||
            !info.sample_rate || info.sample_rate > audio::max_sample_rate)
        {
            return std::nullopt;
        }
        constexpr size_t max_samples = audio::max_sample_bytes / sizeof(int16_t);
        const auto declared_frames = stb_vorbis_stream_length_in_samples(decoder.get());
        if (declared_frames > max_samples / static_cast<size_t>(info.channels))
        {
            return std::nullopt;
        }
        DecodedOgg decoded{
            .channels = static_cast<uint16_t>(info.channels),
            .sample_rate = info.sample_rate,
        };
        decoded.samples.reserve(static_cast<size_t>(declared_frames) * info.channels);
        constexpr size_t decode_chunk_frames = 4096;
        std::array<short, decode_chunk_frames * audio::max_channels> chunk{};
        for (;;)
        {
            if (cancellation.stop_requested())
            {
                return std::nullopt;
            }
            const auto frames = stb_vorbis_get_samples_short_interleaved(
                decoder.get(), info.channels, chunk.data(), static_cast<int>(chunk.size()));
            if (frames <= 0)
            {
                break;
            }
            const auto samples = static_cast<size_t>(frames) * info.channels;
            if (samples > max_samples - decoded.samples.size())
            {
                return std::nullopt;
            }
            decoded.samples.insert(decoded.samples.end(), chunk.begin(), chunk.begin() + samples);
        }
        if (decoded.samples.empty() || stb_vorbis_get_error(decoder.get()) != VORBIS__no_error)
        {
            return std::nullopt;
        }
        return decoded;
    }
} // namespace kh3ap
