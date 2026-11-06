#pragma once
#include "kh02_optional_audio.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace kh3ap
{
    class Kh02MovieFrameQueue
    {
      public:
        struct Frame
        {
            int number = -1;
            std::vector<uint8_t> pixels;
        };

        void prepare(std::size_t frame_bytes)
        {
            constexpr std::size_t audible_lookahead_slots = 8;
            frames.resize(audible_lookahead_slots);
            for (auto& frame : frames)
            {
                frame.pixels.resize(frame_bytes);
            }
        }

        void clear()
        {
            frames.clear();
        }

        void reset_received()
        {
            last_received = -1;
        }

        auto received() const -> int
        {
            return last_received;
        }

        auto free_slot() -> Frame*
        {
            const auto free =
                std::find_if(frames.begin(), frames.end(), [](const Frame& frame) { return frame.number < 0; });
            return free == frames.end() ? nullptr : &*free;
        }

        void accept(Frame& frame, int number)
        {
            last_received = number;
            frame.number = number;
        }

        auto due(uint64_t audio_frame, int shown) -> Frame*
        {
            Frame* due = nullptr;
            for (auto& frame : frames)
            {
                if (frame.number > shown && uint64_t(frame.number) <= audio_frame &&
                    (!due || frame.number > due->number))
                {
                    due = &frame;
                }
            }
            return due;
        }

        auto latest(int shown) -> Frame*
        {
            return latest_kh02_video_frame(std::span(frames), shown);
        }

        void retire(int shown)
        {
            for (auto& frame : frames)
            {
                if (frame.number <= shown)
                {
                    frame.number = -1;
                }
            }
        }

      private:
        std::vector<Frame> frames;
        int last_received = -1;
    };
} // namespace kh3ap
