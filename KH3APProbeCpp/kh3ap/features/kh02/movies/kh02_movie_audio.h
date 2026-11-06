#pragma once
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

namespace kh02movie
{
    class SharedFrames;
}

namespace kh3ap
{
    class Kh02MovieAudio
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

      public:
        Kh02MovieAudio(int channels, int sample_rate);
        ~Kh02MovieAudio();
        void tick(kh02movie::SharedFrames& shared, float gain);
        void set_paused(bool paused);
        void stop() noexcept;
        auto drained(kh02movie::SharedFrames& shared) const -> bool;
        auto played_frames() const -> uint64_t;
        auto snapshot() const -> nlohmann::json;
    };
} // namespace kh3ap
