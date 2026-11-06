#pragma once
#include "kh02_movie_frame_queue.h"
#include <cstdint>
#include <vector>

namespace kh3ap
{
    class Kh02MovieFramePresenter
    {
      public:
        // Two 30 FPS presentation intervals preserve the final uploaded picture.
        static constexpr uint64_t final_frame_hold_ms = 67;

        void prepare_audio_queue(std::size_t frame_bytes)
        {
            video_queue.prepare(frame_bytes);
        }

        void discard_audio_queue()
        {
            video_queue.clear();
        }

        void clear()
        {
            pixels.clear();
            video_queue.clear();
        }

        void reset_request()
        {
            requested = 0;
            accepted = 0;
            shown = -1;
            video_queue.reset_received();
            last_upload_at = 0;
        }

        template <class Source, class AudioClock, class Upload, class Show, class Now>
        void present(Source& source, bool audio_clock_active, AudioClock&& audio_frame, Upload&& upload, Show&& show,
                     Now&& now_ms)
        {
            int number = -1;
            const std::vector<uint8_t>* presented_pixels = nullptr;
            if (audio_clock_active)
            {
                auto* free = video_queue.free_slot();
                // Full lookahead is backpressure; keep presenting against audio
                // before consuming more, without dropping the queued frames.
                if (free && source.consume(video_queue.received(), number, free->pixels))
                {
                    video_queue.accept(*free, number);
                    ++requested;
                }
                auto* due = video_queue.due(audio_frame(), shown);
                number = due ? due->number : -1;
                if (due)
                {
                    presented_pixels = &due->pixels;
                }
            }
            else
            {
                auto* queued = video_queue.latest(shown);
                if (queued)
                {
                    number = queued->number;
                    presented_pixels = &queued->pixels;
                }
                int decoded_number = -1;
                if (source.consume(shown, decoded_number, pixels))
                {
                    ++requested;
                    if (decoded_number > number)
                    {
                        number = decoded_number;
                        presented_pixels = &pixels;
                    }
                }
            }
            if (presented_pixels)
            {
                upload(*presented_pixels);
                ++accepted;
                shown = number;
                video_queue.retire(shown);
                last_upload_at = now_ms();
                if (accepted == 1)
                {
                    show();
                }
            }
        }

        auto ready_to_finish(int latest, uint64_t now_ms) const -> bool
        {
            return latest >= 0 && shown >= latest && now_ms - last_upload_at >= final_frame_hold_ms;
        }

        auto requested_frames() const -> int
        {
            return requested;
        }

        auto accepted_frames() const -> int
        {
            return accepted;
        }

        auto shown_frame() const -> int
        {
            return shown;
        }

        auto received_frame() const -> int
        {
            return video_queue.received();
        }

      private:
        std::vector<uint8_t> pixels;
        Kh02MovieFrameQueue video_queue;
        int requested{}, accepted{}, shown{-1};
        uint64_t last_upload_at{};
    };
} // namespace kh3ap
