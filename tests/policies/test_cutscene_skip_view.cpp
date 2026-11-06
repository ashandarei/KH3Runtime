#include "kh3ap/features/story/cutscenes/cutscene_skip_view_policy.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace kh3ap;
    assert(cutscene_skip_view_scene(
        L"/Game/Cutscene/Localization/ja/mi/cutscene_mi01_mi352.cutscene_mi01_mi352_C"));
    assert(cutscene_skip_view_scene(
        L"/Game/Cutscene/Localization/en/mi/cutscene_mi01_mi353.cutscene_mi01_mi353_C"));
    assert(!cutscene_skip_view_scene(
        L"/Game/Cutscene/Localization/ja/mi/cutscene_mi50_mi351.cutscene_mi50_mi351_C"));
    assert(!cutscene_skip_view_scene(
        L"/Game/Other/mi/cutscene_mi01_mi352.cutscene_mi01_mi352_C"));
    assert(!cutscene_skip_view_scene(
        L"/Game/Cutscene/Localization/ja/mi/cutscene_mi01_mi352.cutscene_mi01_mi352_C_0"));

    const CutsceneSkipViewReadiness playable{true, true, true, true, false, false};
    assert(cutscene_skip_view_ready(playable));
    auto state = playable;
    state.same_world = false;
    assert(!cutscene_skip_view_ready(state));
    state = playable;
    state.source_retired = false;
    assert(!cutscene_skip_view_ready(state));
    state = playable;
    state.control_ready = false;
    assert(!cutscene_skip_view_ready(state));
    state = playable;
    state.loading_closed = false;
    assert(!cutscene_skip_view_ready(state));
    state = playable;
    state.paused = true;
    assert(!cutscene_skip_view_ready(state));
    state = playable;
    state.cinematic = true;
    assert(!cutscene_skip_view_ready(state));

    assert(cutscene_skip_view_stuck(1.f, false));
    assert(!cutscene_skip_view_stuck(1.f, true));
    assert(!cutscene_skip_view_stuck(0.f, false));
    assert(!cutscene_skip_view_stuck(0.5f, false));
    assert(!cutscene_skip_view_stuck(2.f, false));
    assert(!cutscene_skip_view_stuck(std::numeric_limits<float>::quiet_NaN(), false));
}
