#pragma once
#include <cstdint>
#include <stdexcept>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    inline constexpr uint8_t maximum_game_volume_setting = 10;

    inline auto kh02_movie_gain(uint8_t master, uint8_t effects) -> float
    {
        if (master > maximum_game_volume_setting || effects > maximum_game_volume_setting)
        {
            throw std::runtime_error("movie_audio_game_volume");
        }
        constexpr float combined_volume_scale = maximum_game_volume_setting * maximum_game_volume_setting;
        return master * effects / combined_volume_scale;
    }

    auto kh02_movie_volume(RC::Unreal::UObject* world) -> float;
} // namespace kh3ap
