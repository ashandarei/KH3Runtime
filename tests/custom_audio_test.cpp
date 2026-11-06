#include "kh3ap/features/music/audio/audio_limits.h"
#include "kh3ap/features/music/audio/custom_audio.h"
#include "kh3ap/features/music/audio/ogg_decoder.h"

#include <Windows.h>
#include <mmreg.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>

namespace
{
    using Bytes = std::vector<std::byte>;

    template <class T> auto append(Bytes& bytes, T value) -> void
    {
        const auto* first = reinterpret_cast<const std::byte*>(&value);
        bytes.insert(bytes.end(), first, first + sizeof(value));
    }

    auto chunk(Bytes& bytes, const char (&id)[5], const Bytes& body) -> void
    {
        const auto* first = reinterpret_cast<const std::byte*>(id);
        bytes.insert(bytes.end(), first, first + 4);
        append(bytes, static_cast<uint32_t>(body.size()));
        bytes.insert(bytes.end(), body.begin(), body.end());
        if (body.size() & 1)
        {
            bytes.push_back(std::byte{});
        }
    }

    auto wave(Bytes format, Bytes samples = Bytes(8)) -> Bytes
    {
        Bytes result;
        const std::array header{'R', 'I', 'F', 'F', '\0', '\0', '\0', '\0', 'W', 'A', 'V', 'E'};
        for (const char character : header)
        {
            result.push_back(static_cast<std::byte>(character));
        }
        chunk(result, "fmt ", format);
        chunk(result, "data", samples);
        const auto size = static_cast<uint32_t>(result.size() - 8);
        std::memcpy(result.data() + 4, &size, sizeof(size));
        return result;
    }

    auto format_bytes(WAVEFORMATEX format, size_t size = sizeof(WAVEFORMATEX)) -> Bytes
    {
        Bytes bytes(size);
        std::memcpy(bytes.data(), &format, (std::min)(sizeof(format), size));
        return bytes;
    }
} // namespace

int main(int argc, char** argv)
{
    std::stop_source cancelled;
    cancelled.request_stop();
    assert(!kh3ap::load_custom_audio("unused.wav", cancelled.get_token()));
    assert(!kh3ap::decode_ogg_vorbis({}, cancelled.get_token()));

    WAVEFORMATEX format{WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0};
    const auto pcm = wave(format_bytes(format, 16));
    const auto decoded = kh3ap::parse_custom_wav(pcm);
    assert(decoded && decoded->samples.size() == 8);
    assert(decoded->format.size() == sizeof(WAVEFORMATEX));
    WAVEFORMATEX normalized{};
    std::memcpy(&normalized, decoded->format.data(), sizeof(normalized));
    assert(normalized.cbSize == 0 && normalized.nChannels == 2);
    assert(kh3ap::parse_custom_wav(wave(format_bytes(format))));
    assert(kh3ap::parse_custom_wav(wave(format_bytes(format), Bytes(7)))->samples.size() == 4);
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format), Bytes(3))));
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format), {})));
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format, 17))));
    format.cbSize = 22;
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format))));
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format, sizeof(format) + 22))));
    WAVEFORMATEXTENSIBLE extended{};
    extended.Format = format;
    extended.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    extended.Samples.wValidBitsPerSample = format.wBitsPerSample;
    extended.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    Bytes extensible_bytes;
    append(extensible_bytes, extended);
    assert(kh3ap::parse_custom_wav(wave(extensible_bytes)));
    extended.Format.cbSize = 0;
    extensible_bytes.clear();
    append(extensible_bytes, extended);
    assert(!kh3ap::parse_custom_wav(wave(extensible_bytes)));
    format.cbSize = 0;
    format.nBlockAlign = 0;
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format))));
    format.nBlockAlign = 4;
    format.nChannels = UINT16_MAX;
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format))));
    format.nChannels = 2;
    format.nSamplesPerSec = UINT32_MAX;
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format))));
    format.nSamplesPerSec = 44100;
    format.nAvgBytesPerSec = 1;
    assert(!kh3ap::parse_custom_wav(wave(format_bytes(format))));

    auto corrupt = pcm;
    constexpr size_t data_length_offset = 40;
    uint32_t data_size = 100;
    std::memcpy(corrupt.data() + data_length_offset, &data_size, sizeof(data_size));
    assert(!kh3ap::parse_custom_wav(corrupt));
    data_size = UINT32_MAX;
    std::memcpy(corrupt.data() + data_length_offset, &data_size, sizeof(data_size));
    assert(kh3ap::parse_custom_wav(corrupt));
    for (size_t size = 0; size < pcm.size() - 4; ++size)
    {
        assert(!kh3ap::parse_custom_wav(std::span{pcm}.first(size)));
    }
    std::mt19937 random(42);
    for (size_t iteration = 0; iteration < 2048; ++iteration)
    {
        Bytes bytes(random() % 4096);
        for (auto& byte : bytes)
        {
            byte = static_cast<std::byte>(random());
        }
        assert(!kh3ap::parse_custom_wav(bytes));
        assert(!kh3ap::decode_ogg_vorbis(bytes));
    }
    for (size_t iteration = 0; iteration < 1024; ++iteration)
    {
        auto bytes = pcm;
        for (size_t mutation = 0; mutation < 4; ++mutation)
        {
            bytes[random() % bytes.size()] = static_cast<std::byte>(random());
        }
        if (auto parsed = kh3ap::parse_custom_wav(bytes))
        {
            WAVEFORMATEX actual{};
            std::memcpy(&actual, parsed->format.data(), sizeof(actual));
            assert(actual.cbSize <= parsed->format.size() - sizeof(actual));
            assert(actual.nBlockAlign && parsed->samples.size() % actual.nBlockAlign == 0);
        }
    }

    using kh3ap::custom_music_loop_region;
    using Region = std::pair<uint32_t, uint32_t>;
    assert(custom_music_loop_region({}, {}, 100, 1000) == Region(0, 0));
    assert(custom_music_loop_region(1.0, 3.0, 100, 1000) == Region(100, 200));
    assert(custom_music_loop_region(1.0, 1e300, 100, 1000) == Region(100, 0));
    assert(custom_music_loop_region(1e300, {}, 100, 1000) == Region(0, 0));
    assert(custom_music_loop_region(-5.0, 3.0, 100, 1000) == Region(0, 300));
    assert(custom_music_loop_region((std::numeric_limits<double>::quiet_NaN)(), 3.0, 100, 1000) ==
           Region(0, 300));
    assert(custom_music_loop_region(1.0, (std::numeric_limits<double>::infinity)(), 100, 1000) ==
           Region(100, 0));

    const auto file = std::filesystem::temp_directory_path() /
                      (L"kh3-audio-" + std::to_wstring(GetCurrentProcessId()) + L".wav");
    {
        std::ofstream output(file, std::ios::binary);
        output.write(reinterpret_cast<const char*>(pcm.data()), pcm.size());
    }
    assert(kh3ap::load_custom_audio(file));
    std::filesystem::resize_file(file, kh3ap::audio::max_file_bytes + 1);
    assert(!kh3ap::load_custom_audio(file));
    std::filesystem::remove(file);

    if (argc > 1)
    {
        // Optional locally generated Vorbis fixture; no game assets are required.
        const auto ogg = kh3ap::load_custom_audio(argv[1]);
        assert(ogg && !ogg->samples.empty());
        std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
        Bytes encoded(static_cast<size_t>(input.tellg()));
        input.seekg(0);
        assert(input.read(reinterpret_cast<char*>(encoded.data()), encoded.size()));
        for (size_t iteration = 0; iteration < 256; ++iteration)
        {
            auto mutated = encoded;
            mutated[random() % mutated.size()] ^= std::byte{0xff};
            (void)kh3ap::decode_ogg_vorbis(mutated);
        }
    }
}
