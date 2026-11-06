#pragma once
#include <span>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    // Only device/output operations throw this type. Format, shared-memory and
    // source decoder validation keep their ordinary fatal errors.
    class Kh02AudioOutputError final : public std::runtime_error
    {
      public:
        using std::runtime_error::runtime_error;
    };

    template <class Operation, class Warning> void try_kh02_audio_output(Operation&& operation, Warning&& warning)
    {
        try
        {
            std::forward<Operation>(operation)();
        }
        catch (const Kh02AudioOutputError& error)
        {
            std::forward<Warning>(warning)(error.what());
        }
    }

    // Call only around optional native music execution, after operand, asset
    // and caller ownership checks. Those guards must remain outside this scope.
    template <class Operation, class Warning> void try_kh02_music_execution(Operation&& operation, Warning&& warning)
    {
        try
        {
            std::forward<Operation>(operation)();
        }
        catch (const std::runtime_error& error)
        {
            std::forward<Warning>(warning)(error.what());
        }
    }

    // Preserve queued video when its audio output disappears, including a final
    // frame already consumed from shared memory. No decoder completion is made up.
    template <class Frame, size_t Extent>
    auto latest_kh02_video_frame(std::span<Frame, Extent> frames, int shown) -> Frame*
    {
        Frame* latest = nullptr;
        for (auto& frame : frames)
        {
            if (frame.number > shown && (!latest || frame.number > latest->number))
            {
                latest = &frame;
            }
        }
        return latest;
    }
} // namespace kh3ap
