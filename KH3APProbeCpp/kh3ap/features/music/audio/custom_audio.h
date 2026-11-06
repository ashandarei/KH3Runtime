#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <span>
#include <utility>
#include <vector>

namespace kh3ap
{
    struct CustomAudioData
    {
        // A complete WAVEFORMATEX, including any declared extension.
        std::vector<std::byte> format;
        std::vector<std::byte> samples;
        std::chrono::milliseconds load_time{};
    };

    [[nodiscard]] auto parse_custom_wav(std::span<const std::byte> bytes)
        -> std::optional<CustomAudioData>;
    [[nodiscard]] auto load_custom_audio(const std::filesystem::path& file,
                                         std::stop_token cancellation = {})
        -> std::optional<CustomAudioData>;
    [[nodiscard]] auto custom_music_loop_region(std::optional<double> start,
                                                std::optional<double> end, uint32_t sample_rate,
                                                size_t frames) -> std::pair<uint32_t, uint32_t>;
} // namespace kh3ap
