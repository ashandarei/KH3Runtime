#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace kh3ap
{
    class Kh02MoviePlaybackState
    {
      public:
        enum class Phase
        {
            idle,
            decoding,
            uploading,
            ready,
            playing,
            stream_playing,
            completed,
            stopped,
            error
        };
        Phase phase{Phase::idle};
        uint64_t generation{};
        bool resume_requested{};
        bool finished{};

        auto name() const -> std::string_view
        {
            switch (phase)
            {
            case Phase::idle:
                return "idle";
            case Phase::decoding:
                return "decoding";
            case Phase::uploading:
                return "uploading";
            case Phase::ready:
                return "ready";
            case Phase::playing:
                return "playing";
            case Phase::stream_playing:
                return "stream_playing";
            case Phase::completed:
                return "completed";
            case Phase::stopped:
                return "stopped";
            case Phase::error:
                return "error";
            }
            return "error";
        }

        auto completed() const -> bool
        {
            return phase == Phase::completed && finished;
        }

        auto needs_tick() const -> bool
        {
            return phase != Phase::idle && phase != Phase::stopped && phase != Phase::completed &&
                   phase != Phase::error;
        }

        void reset_request()
        {
            resume_requested = false;
            finished = false;
        }

        void resume()
        {
            if (phase == Phase::playing || phase == Phase::stream_playing || completed())
            {
                return;
            }
            if (phase != Phase::decoding && phase != Phase::uploading && phase != Phase::ready)
            {
                throw std::runtime_error("movie_bridge_resume_without_preparation");
            }
            resume_requested = true;
        }

        void stop()
        {
            if (phase != Phase::idle && phase != Phase::completed)
            {
                phase = Phase::stopped;
            }
        }

        void complete()
        {
            finished = true;
            phase = Phase::completed;
        }

        void retire()
        {
            reset_request();
            phase = Phase::idle;
        }
    };
} // namespace kh3ap
