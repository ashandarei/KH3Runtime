#include "kh3ap/features/music/audio/custom_audio.h"
#include "kh3ap/features/music/audio/audio_limits.h"
#include "kh3ap/features/music/audio/ogg_decoder.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mmreg.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <limits>
#include <string>

namespace kh3ap
{
    namespace
    {
        constexpr size_t riff_header_bytes = 12;
        constexpr size_t riff_form_type_offset = 8;
        constexpr size_t chunk_id_bytes = 4;
        constexpr size_t chunk_header_bytes = 8;
        constexpr size_t chunk_size_offset = 4;
        constexpr size_t pcm_format_bytes = 16;
        constexpr size_t max_format_bytes = sizeof(WAVEFORMATEX) + UINT16_MAX;
        constexpr unsigned pcm_sample_bits = 16;
        constexpr WORD float_sample_bits = 32;
        constexpr std::array<WORD, 4> pcm_bit_depths{8, 16, 24, 32};

        auto valid_format(std::span<const std::byte> bytes, const WAVEFORMATEX& format) -> bool
        {
            WORD encoding = format.wFormatTag;
            if (encoding == WAVE_FORMAT_EXTENSIBLE)
            {
                constexpr auto extension_bytes =
                    sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                if (bytes.size() < sizeof(WAVEFORMATEXTENSIBLE) || format.cbSize < extension_bytes)
                {
                    return false;
                }
                WAVEFORMATEXTENSIBLE extended{};
                std::memcpy(&extended, bytes.data(), sizeof(extended));
                if (IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
                {
                    encoding = WAVE_FORMAT_PCM;
                }
                else if (IsEqualGUID(extended.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
                {
                    encoding = WAVE_FORMAT_IEEE_FLOAT;
                }
                else
                {
                    return false;
                }
                if (extended.Samples.wValidBitsPerSample > format.wBitsPerSample)
                {
                    return false;
                }
            }
            else if (format.cbSize != 0)
            {
                return false;
            }
            const bool supported_bits =
                encoding == WAVE_FORMAT_PCM
                    ? std::ranges::find(pcm_bit_depths, format.wBitsPerSample) !=
                          pcm_bit_depths.end()
                    : encoding == WAVE_FORMAT_IEEE_FLOAT &&
                          format.wBitsPerSample == float_sample_bits;
            if (!supported_bits)
            {
                return false;
            }
            const auto block_bytes = format.nChannels * (format.wBitsPerSample / CHAR_BIT);
            return format.nBlockAlign == block_bytes &&
                   format.nAvgBytesPerSec == format.nSamplesPerSec * block_bytes;
        }

        auto chunk_is(std::span<const std::byte> bytes, size_t offset,
                      const char (&id)[chunk_id_bytes + 1]) -> bool
        {
            return std::memcmp(bytes.data() + offset, id, chunk_id_bytes) == 0;
        }

        auto from_ogg(DecodedOgg ogg) -> CustomAudioData
        {
            WAVEFORMATEX format{};
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.nChannels = ogg.channels;
            format.nSamplesPerSec = ogg.sample_rate;
            format.wBitsPerSample = pcm_sample_bits;
            format.nBlockAlign = static_cast<WORD>(ogg.channels * sizeof(int16_t));
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            CustomAudioData audio;
            audio.format.resize(sizeof(format));
            std::memcpy(audio.format.data(), &format, sizeof(format));
            audio.samples.resize(ogg.samples.size() * sizeof(int16_t));
            std::memcpy(audio.samples.data(), ogg.samples.data(), audio.samples.size());
            return audio;
        }
    } // namespace

    auto parse_custom_wav(std::span<const std::byte> bytes) -> std::optional<CustomAudioData>
    {
        if (bytes.size() < riff_header_bytes || bytes.size() > audio::max_file_bytes ||
            !chunk_is(bytes, 0, "RIFF") || !chunk_is(bytes, riff_form_type_offset, "WAVE"))
        {
            return std::nullopt;
        }
        CustomAudioData result;
        size_t offset = riff_header_bytes;
        while (bytes.size() - offset >= chunk_header_bytes)
        {
            uint32_t declared_size{};
            std::memcpy(&declared_size, bytes.data() + offset + chunk_size_offset,
                        sizeof(declared_size));
            size_t size = declared_size;
            const size_t body = offset + chunk_header_bytes;
            if (size > bytes.size() - body)
            {
                // Streaming WAV writers use this sentinel when the final size is unknown.
                if (!chunk_is(bytes, offset, "data") || declared_size != UINT32_MAX)
                {
                    return std::nullopt;
                }
                size = bytes.size() - body;
            }
            if (chunk_is(bytes, offset, "fmt "))
            {
                if (!result.format.empty() || size < pcm_format_bytes || size > max_format_bytes)
                {
                    return std::nullopt;
                }
                WAVEFORMATEX format{};
                if (size == pcm_format_bytes)
                {
                    std::memcpy(&format, bytes.data() + body, pcm_format_bytes);
                    if (format.wFormatTag != WAVE_FORMAT_PCM &&
                        format.wFormatTag != WAVE_FORMAT_IEEE_FLOAT)
                    {
                        return std::nullopt;
                    }
                }
                else
                {
                    if (size < sizeof(format))
                    {
                        return std::nullopt;
                    }
                    std::memcpy(&format, bytes.data() + body, sizeof(format));
                    if (format.cbSize > size - sizeof(format))
                    {
                        return std::nullopt;
                    }
                }
                if (!format.nBlockAlign || !format.nChannels ||
                    format.nChannels > audio::max_channels || !format.nSamplesPerSec ||
                    format.nSamplesPerSec > audio::max_sample_rate ||
                    !valid_format(bytes.subspan(body, size), format))
                {
                    return std::nullopt;
                }
                result.format.assign(bytes.begin() + body, bytes.begin() + body + size);
                result.format.resize((std::max)(size, sizeof(format)));
            }
            else if (chunk_is(bytes, offset, "data"))
            {
                if (!result.samples.empty() || !size || size > audio::max_sample_bytes)
                {
                    return std::nullopt;
                }
                result.samples.assign(bytes.begin() + body, bytes.begin() + body + size);
            }
            offset = body + size;
            if ((size & 1) && offset < bytes.size())
            {
                ++offset;
            }
        }
        if (result.format.empty() || result.samples.empty())
        {
            return std::nullopt;
        }
        WAVEFORMATEX format{};
        std::memcpy(&format, result.format.data(), sizeof(format));
        result.samples.resize(result.samples.size() - result.samples.size() % format.nBlockAlign);
        if (result.samples.empty())
        {
            return std::nullopt;
        }
        return result;
    }

    auto load_custom_audio(const std::filesystem::path& file, std::stop_token cancellation)
        -> std::optional<CustomAudioData>
    {
        if (cancellation.stop_requested())
        {
            return std::nullopt;
        }
        std::ifstream stream{file, std::ios::binary | std::ios::ate};
        if (!stream)
        {
            return std::nullopt;
        }
        const auto size = stream.tellg();
        if (size <= 0 || size > static_cast<std::streamoff>(audio::max_file_bytes))
        {
            return std::nullopt;
        }
        std::vector<std::byte> bytes(static_cast<size_t>(size));
        stream.seekg(0);
        if (!stream.read(reinterpret_cast<char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size())))
        {
            return std::nullopt;
        }
        if (cancellation.stop_requested())
        {
            return std::nullopt;
        }
        auto extension = file.extension().wstring();
        for (auto& character : extension)
        {
            character = static_cast<wchar_t>(std::towlower(character));
        }
        if (extension == L".ogg")
        {
            auto ogg = decode_ogg_vorbis(bytes, cancellation);
            return ogg ? std::optional{from_ogg(std::move(*ogg))} : std::nullopt;
        }
        return parse_custom_wav(bytes);
    }

    auto custom_music_loop_region(std::optional<double> start, std::optional<double> end,
                                  uint32_t sample_rate, size_t frames)
        -> std::pair<uint32_t, uint32_t>
    {
        if (!sample_rate || frames > (std::numeric_limits<uint32_t>::max)())
        {
            return {};
        }
        const auto frame_at = [sample_rate, frames](double seconds) -> size_t
        {
            if (std::isnan(seconds) || seconds <= 0.0)
            {
                return 0;
            }
            const double position = seconds * sample_rate;
            if (!std::isfinite(position) || position >= static_cast<double>(frames))
            {
                return frames;
            }
            return static_cast<size_t>(position);
        };
        const size_t begin = start ? frame_at(*start) : 0;
        const size_t finish = end ? frame_at(*end) : frames;
        if (begin >= finish)
        {
            return {};
        }
        return {static_cast<uint32_t>(begin),
                static_cast<uint32_t>(finish == frames ? 0 : finish - begin)};
    }
} // namespace kh3ap
