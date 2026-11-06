#include "../../../KH3APProbeCpp/kh3ap/features/kh02/movies/kh02_movie_frame_presenter.h"
#include <cassert>

struct Source
{
    int next{}, calls{};

    bool consume(int, int& number, std::vector<uint8_t>& pixels)
    {
        ++calls;
        number = next++;
        pixels = {static_cast<uint8_t>(number)};
        return true;
    }
};

int main()
{
    kh3ap::Kh02MovieFramePresenter presenter;
    Source source;
    uint64_t clock = 0, now = 100;
    int uploads = 0, shown = 0, last_pixel = -1;
    const auto present = [&](bool audio)
    {
        presenter.present(
            source, audio, [&] { return clock; },
            [&](const auto& pixels)
            {
                ++uploads;
                last_pixel = pixels.front();
            },
            [&] { ++shown; }, [&] { return now; });
    };
    presenter.prepare_audio_queue(1);
    present(true);
    assert(uploads == 1 && shown == 1 && presenter.shown_frame() == 0);
    assert(!presenter.ready_to_finish(0, 166));
    assert(presenter.ready_to_finish(0, 167));
    present(true);
    assert(uploads == 1 && presenter.received_frame() == 1);
    clock = 1;
    present(true);
    assert(uploads == 2 && presenter.shown_frame() == 1);
    // Losing the audio clock selects the newest queued/decoded picture.
    present(false);
    assert(last_pixel == 3 && presenter.shown_frame() == 3 && shown == 1);
    presenter.clear();
    assert(presenter.shown_frame() == 3 && presenter.accepted_frames() == 3);
    presenter.reset_request();
    presenter.prepare_audio_queue(1);
    source = {};
    clock = 0;
    for (int count = 0; count < 10; ++count)
    {
        present(true);
    }
    assert(source.calls == 9); // One displayed frame plus eight lookahead slots.
    assert(presenter.requested_frames() == 9 && presenter.shown_frame() == 0);
    clock = 8;
    present(true);
    assert(source.calls == 9 && presenter.shown_frame() == 8);
    present(true);
    assert(source.calls == 10 && presenter.received_frame() == 9);
    assert(!presenter.ready_to_finish(-1, now + 100));
    assert(!presenter.ready_to_finish(9, now + 100));
}
