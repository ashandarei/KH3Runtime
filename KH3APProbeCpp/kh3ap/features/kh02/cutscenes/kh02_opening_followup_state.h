#pragma once
#include <algorithm>

namespace kh3ap
{
    class Kh02OpeningFollowupState
    {
      public:
        void reset()
        {
            observed_playing = false;
            music_handoff_ready = false;
            maximum_position = 0;
        }

        void accept_music_handoff()
        {
            music_handoff_ready = true;
        }

        auto observed() const -> bool
        {
            return observed_playing;
        }

        auto handoff_ready() const -> bool
        {
            return music_handoff_ready;
        }

        auto maximum() const -> float
        {
            return maximum_position;
        }

        auto observe(bool valid, bool playing, float position, bool script_alive, bool movie_playing) -> bool
        {
            observed_playing |= playing;
            if (valid)
            {
                maximum_position = std::max(maximum_position, position);
            }
            return observed_playing && !playing && !script_alive && music_handoff_ready && !movie_playing;
        }

      private:
        bool observed_playing{}, music_handoff_ready{};
        float maximum_position{};
    };
} // namespace kh3ap
