#include "kh3ap/features/kh02/cutscenes/kh02_scene_catalog.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_session_state.h"
#include "kh3ap/features/kh02/cutscenes/kh02_opening_followup_state.h"
#include "kh3ap/features/kh02/story/kh02_arrival_readiness.h"
#include "kh3ap/features/kh02/story/kh02_islands_resume_policy.h"
#include "kh3ap/features/kh02/movies/kh02_movie_settings.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

int main()
{
    using namespace kh3ap;
    assert(kh02_islands_entry_checkpoint(500) == 410);
    assert(kh02_islands_entry_checkpoint(510) == 410);
    assert(kh02_islands_entry_checkpoint(519) == 410);
    for (const int checkpoint : {0, 400, 410, 499, 520, 521, 530, 620, 9999})
    {
        assert(kh02_islands_entry_checkpoint(checkpoint) == checkpoint);
    }
    const auto opening = kh02_scene_binding(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw011.dw011", L"dw011");
    assert(opening.accepted() && opening.opening && opening.scene && !opening.camera);
    assert(std::wstring_view(opening.group) == L"TresInterpGroup_22");
    const auto forest = kh02_scene_binding(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw022.dw022", L"dw022");
    assert(forest.accepted() && !forest.opening);
    assert(std::wstring_view(forest.group) == L"TresInterpGroup_2");
    const auto ending = kh02_scene_binding(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw025a.dw025a", L"dw025a");
    assert(ending.accepted());
    assert(std::wstring_view(ending.group) == L"TresInterpGroup_10");
    assert(
        !kh02_scene_binding(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw025a.dw025a", L"unrelated").accepted());
    assert(!kh02_scene_binding(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw025a_extra.dw025a", L"dw025a")
                .accepted());
    const auto camera = kh02_scene_binding(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms040.ms040s", L"ms040s");
    assert(camera.camera && !camera.scene && !camera.opening);
    assert(!kh02_scene_binding(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms040.other", L"other").accepted());
    assert(
        !kh02_scene_binding(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw013.dw013_2", L"dw013_2").accepted());
    assert(kh02_scene_binding(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw013.dw013_1", L"dw013_1").accepted());
    for (const auto& scene : kh02ending_metadata::scenes)
    {
        const auto binding = kh02_scene_binding(std::wstring(scene.package) + L"." + scene.actor, scene.actor);
        assert(binding.accepted());
        assert(std::wstring_view(binding.group) == scene.group);
    }
    assert(std::string_view(kh02_scene_full_movie(L"/Game/Zutscene/Localization/en/dw/cutscene_dw53_dw033.dw033")) ==
           "Rebellis02/ja/cs_dw033_mv.usm");
    assert(!kh02_scene_full_movie(L"/Game/Zutscene/Localization/en/dw/cutscene_dw53_dw033_extra.dw033"));
    assert(std::string_view(kh02_scene_full_movie(L"/Game/Zutscene/Localization/en/dw/cutscene_dw09_dw026.dw026")) ==
           "Rebellis02/ja/cs_dw026_mv.usm");
    assert(std::string_view(kh02_scene_full_movie(L"/Game/Zutscene/Localization/en/dw/cutscene_dw51_dw027.dw027_1")) ==
           "Rebellis02/ja/cs_dw027_mv.usm");
    assert(!kh02_scene_full_movie(L"/Game/Zutscene/Localization/en/dw/cutscene_dw09_dw026_extra.dw026"));
    for (const auto* sound_track : {L"InterpTrackSound", L"SQEXSEADInterpTrackSound"})
    {
        assert(kh02_full_movie_disables_track(sound_track, false));
        assert(!kh02_full_movie_disables_track(sound_track, true));
    }
    for (const bool matinee_audio : {false, true})
    {
        assert(kh02_full_movie_disables_track(L"InterpTrackEvent", matinee_audio));
        assert(!kh02_full_movie_disables_track(L"TresInterpTrackBGM", matinee_audio));
        assert(!kh02_full_movie_disables_track(L"TresInterpTrackSubtitle", matinee_audio));
    }

    Kh02OpeningFollowupState followup;
    assert(!followup.observe(false, false, 0, false, false));
    followup.accept_music_handoff();
    assert(!followup.observe(false, false, 0, false, false));
    assert(!followup.observe(true, true, 5.f, true, true));
    assert(!followup.observe(true, true, 3.f, true, true));
    assert(followup.observed() && followup.maximum() == 5.f);
    assert(!followup.observe(true, false, 5.f, true, false));
    assert(!followup.observe(false, false, 0, false, true));
    assert(followup.observe(false, false, 0, false, false));
    followup.reset();
    assert(!followup.observed() && !followup.handoff_ready() && followup.maximum() == 0);
    assert(!followup.observe(true, true, 1.f, true, false));
    assert(!followup.observe(false, false, 0, false, false));
    followup.accept_music_handoff();
    assert(followup.observe(false, false, 0, false, false));

    assert(kh02_arrival_view_ready(true, false, 0.f));
    assert(!kh02_arrival_view_ready(false, false, 0.f));
    assert(!kh02_arrival_view_ready(true, true, 0.f));
    assert(!kh02_arrival_view_ready(true, false, kh02_completed_fade_tolerance));
    assert(kh02_arrival_view_ready(true, false, std::nextafter(kh02_completed_fade_tolerance, 0.f)));
    assert(!kh02_arrival_view_ready(true, false, std::numeric_limits<float>::quiet_NaN()));
    assert(kh02_movie_gain(10, 10) == 1.f);
    assert(kh02_movie_gain(0, 10) == 0.f);
    assert(kh02_movie_gain(10, 0) == 0.f);
    assert(kh02_movie_gain(5, 5) == .25f);
    for (const auto invalid : {std::pair<uint8_t, uint8_t>{11, 10}, {10, 11}})
    {
        bool rejected = false;
        try
        {
            kh02_movie_gain(invalid.first, invalid.second);
        }
        catch (const std::runtime_error& error)
        {
            rejected = std::string_view(error.what()) == "movie_audio_game_volume";
        }
        assert(rejected);
    }
}
