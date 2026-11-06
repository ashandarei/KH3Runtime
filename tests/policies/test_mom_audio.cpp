#include "kh3ap/features/treasure/music_chest/mom_audio_state.h"
#include "kh3ap/features/treasure/music_chest/music_pause_policy.h"
#include <cassert>
#include <optional>

int main() {
    kh3ap::RuntimeMoMAudioState audio;
    bool muted = false, already_muted = true, writable = true;
    int reads = 0;
    auto session = [&] {
        return kh3ap::RuntimeMoMAudioState::Session{
            [&]() -> std::optional<bool> { ++reads; return muted; },
            [&](bool value) { if (!writable) return false; muted = value; return true; }
        };
    };
    assert(audio.mute(L"kh3-speakers", session()));
    assert(muted && reads == 1);
    assert(audio.mute(L"kh3-speakers", session()));
    assert(reads == 1); // Never snapshot our own temporary mute.
    assert(audio.mute(L"kh3-headphones", {
        [&]() -> std::optional<bool> { return already_muted; },
        [&](bool value) { already_muted = value; return true; }
    }));
    writable = false;
    assert(!audio.restore()); // Keep failed restores for retry.
    assert(muted && already_muted && !audio.empty());
    writable = true;
    assert(audio.restore());
    assert(!muted && already_muted && audio.empty());
    assert(audio.restore());

    // A failed capture cannot modify a session. A failed mute must retain
    // the original state in case the device applied it before returning error.
    bool wrote = false;
    assert(!audio.mute(L"unreadable", {
        []() -> std::optional<bool> { return std::nullopt; },
        [&](bool) { wrote = true; return true; }
    }));
    assert(!wrote && audio.empty());
    writable = false;
    assert(!audio.mute(L"kh3-speakers", session()));
    assert(!audio.empty());
    writable = true;
    assert(audio.restore() && !muted);

    // Each handoff takes a fresh snapshot, including a user's pre-existing mute.
    muted = true;
    assert(audio.mute(L"kh3-speakers", session()));
    assert(audio.restore() && muted);

    using namespace std::chrono;
    kh3ap::RuntimeMusicPausePolicy pause;
    const auto now = kh3ap::RuntimeMusicPausePolicy::Clock::now();
    for (bool launched : {false, true}) {
        muted = false;
        pause.begin(now);
        assert(audio.mute(L"kh3-speakers", session()));
        if (launched) {
            assert(!pause.should_release(false, now + seconds(1)));
            assert(!pause.should_release(false, now + minutes(10)));
        }
        assert(pause.should_release(true, now + minutes(11)));
        pause.finish();
        assert(audio.restore() && !muted);
    }
}
