#include "../../../KH3APProbeCpp/kh3ap/features/kh02/movies/kh02_movie_playback_state.h"
#include <cassert>

int main()
{
    using State = kh3ap::Kh02MoviePlaybackState;
    using Phase = State::Phase;
    State state;
    assert(!state.needs_tick());
    bool rejected = false;
    try
    {
        state.resume();
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    assert(rejected);
    for (const auto phase : {Phase::decoding, Phase::uploading, Phase::ready})
    {
        state.phase = phase;
        assert(state.needs_tick());
        state.reset_request();
        state.resume();
        assert(state.resume_requested);
    }
    state.phase = Phase::stream_playing;
    state.resume();
    state.complete();
    assert(state.completed());
    assert(!state.needs_tick());
    assert(state.name() == "completed");
    // Resource stop after real EOF must retain late WaitForVideoFinish behavior.
    state.stop();
    assert(state.completed());
    state.resume();
    state.retire();
    assert(state.phase == Phase::idle && !state.finished && !state.resume_requested);
    state.phase = Phase::ready;
    state.stop();
    assert(state.phase == Phase::stopped && !state.needs_tick());
    state.phase = Phase::error;
    assert(state.name() == "error" && !state.needs_tick());
}
