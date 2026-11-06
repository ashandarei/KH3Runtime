#pragma once
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace kh3ap
{
    // Engine-independent sampling lifecycle. A native control fixture owns this
    // history; source movies continue to use the shared-frame bridge's history.
    class Kh02NativeMovieState
    {
      public:
        static constexpr std::size_t event_limit = 128;

        void begin(bool enabled)
        {
            history = nlohmann::json::array();
            observing = enabled;
            observed_playing = false;
        }

        auto needs_sample(bool world_valid, bool final_sample = false) -> bool
        {
            if (!observing && !final_sample)
            {
                return false;
            }
            if (!world_valid || history.size() >= event_limit)
            {
                observing = false;
                return false;
            }
            return true;
        }

        void record(bool playing, nlohmann::json state, uint64_t now, bool final_sample = false)
        {
            if (!observing && !final_sample)
            {
                return;
            }
            if (history.size() >= event_limit)
            {
                observing = false;
                return;
            }
            observed_playing |= playing;
            // Backend handler ticks advance without a presentation state change.
            state.erase("handler_ticks");
            state["playing"] = playing;
            ++sample_count;
            if (history.empty() || history.back()["state"] != state)
            {
                history.push_back({{"at_ms", now}, {"state", std::move(state)}});
            }
            if (final_sample || (observed_playing && !playing))
            {
                observing = false;
            }
        }

        void retire()
        {
            observing = false;
        }

        auto active() const -> bool
        {
            return observing;
        }

        auto samples() const -> uint64_t
        {
            return sample_count;
        }

        auto events() const -> const nlohmann::json&
        {
            return history;
        }

      private:
        bool observing{};
        bool observed_playing{};
        uint64_t sample_count{};
        nlohmann::json history = nlohmann::json::array();
    };
} // namespace kh3ap
