#pragma once
#include <string_view>

namespace kh3ap
{
    constexpr bool kh02_full_movie_disables_track(std::wstring_view track, bool matinee_audio)
    {
        const bool sound = track == L"InterpTrackSound" || track == L"SQEXSEADInterpTrackSound";
        return track == L"InterpTrackEvent" || (!matinee_audio && sound);
    }

    enum class Kh02HeldMatineeAction
    {
        None,
        Resume,
        Retire
    };

    // The movie EOF hold is owned presentation state. Natural completion may
    // release it, but retiring a map generation must never resume its graph.
    constexpr auto kh02_held_matinee_action(bool owns_hold, bool playing, bool paused, bool movie_completed,
                                            bool retiring) -> Kh02HeldMatineeAction
    {
        if (!owns_hold || !playing)
        {
            return Kh02HeldMatineeAction::None;
        }
        if (retiring)
        {
            return Kh02HeldMatineeAction::Retire;
        }
        if (movie_completed && paused)
        {
            return Kh02HeldMatineeAction::Resume;
        }
        return Kh02HeldMatineeAction::None;
    }
} // namespace kh3ap
