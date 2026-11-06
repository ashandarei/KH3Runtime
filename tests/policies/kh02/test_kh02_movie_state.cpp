#include "kh3ap/features/kh02/movies/kh02_movie_state.h"
#include <cassert>

using kh3ap::Kh02NativeMovieState;
using Json = nlohmann::json;

int main()
{
    Kh02NativeMovieState movie;
    assert(!movie.needs_sample(true));
    movie.begin(true);

    // A prepared movie may wait before it starts. That idle sample must not
    // retire observation before the first actual playback sample.
    assert(movie.needs_sample(true));
    movie.record(false, Json{{"prepared", true}}, 100);
    assert(movie.active());
    assert(movie.needs_sample(true));
    movie.record(true, Json{{"prepared", true}, {"handler_ticks", 1}}, 200);
    movie.record(true, Json{{"prepared", true}, {"handler_ticks", 100}}, 300);
    assert(movie.samples() == 3);
    assert(movie.events().size() == 2);
    assert(!movie.events().back()["state"].contains("handler_ticks"));
    assert(movie.events().back()["at_ms"] == 200);
    movie.record(false, Json{{"prepared", false}}, 400);
    assert(!movie.active());
    assert(!movie.needs_sample(true));

    // Explicit final sampling works after automatic retirement, then leaves
    // polling disabled. Starting a source-movie fixture clears native history
    // without enabling native polling or resetting the lifetime sample count.
    assert(movie.needs_sample(true, true));
    movie.record(false, Json::object(), 500, true);
    assert(!movie.active());
    movie.begin(false);
    assert(movie.events().empty());
    assert(movie.samples() == 5);
    assert(!movie.needs_sample(true));

    // World loss and world departure stop polling without requiring any native
    // backend read. Diagnostic history remains available after departure.
    movie.begin(true);
    assert(!movie.needs_sample(false));
    assert(!movie.active());
    assert(!movie.needs_sample(false, true));
    movie.begin(true);
    movie.record(true, Json::object(), 600);
    movie.retire();
    assert(!movie.needs_sample(true));
    const auto retired_samples = movie.samples();
    movie.record(false, Json::object(), 700);
    assert(movie.samples() == retired_samples);
    assert(movie.events().size() == 1);

    // Changing decoder states must stay bounded even when playback never ends.
    movie.begin(true);
    for (std::size_t index = 0; index < Kh02NativeMovieState::event_limit; ++index)
    {
        assert(movie.needs_sample(true));
        movie.record(true, Json{{"decoder_state", index}}, index);
    }
    assert(!movie.needs_sample(true));
    assert(!movie.active());
    assert(!movie.needs_sample(true, true));
    assert(movie.events().size() == Kh02NativeMovieState::event_limit);
    const auto full_samples = movie.samples();
    movie.record(true, Json{{"decoder_state", "late_completion"}}, 999, true);
    assert(movie.samples() == full_samples);
    assert(movie.events().size() == Kh02NativeMovieState::event_limit);
}
