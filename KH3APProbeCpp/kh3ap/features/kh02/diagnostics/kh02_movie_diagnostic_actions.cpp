#include "kh02_diagnostic_action_helpers.h"
#include "kh3ap/features/kh02/story/kh02_gameplay_binding.h"
#include "kh3ap/features/kh02/cutscenes/kh02_cutscene_session.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include "kh3ap/features/kh02/movies/kh02_movie_probe.h"

namespace kh3ap
{
    using namespace kh02diagnostic_detail;

    void Kh02MovieDiagnosticActions::execute(UObject* pawn, UObject* w, Action action, Kh02MovieActionServices services)
    {
        auto& gameplay = services.gameplay;
        auto& cutscenes = services.cutscenes;
        auto& movie_bridge = services.movie_bridge;
        auto& native_movie = services.native_movie;
        auto& movie_fixture = services.movie_fixture;
        auto& native_movie_fixture = services.native_movie_fixture;
        auto& movie_calls = services.movie_calls;

        if (gameplay.world.Get() != w || gameplay.pawn.Get() != pawn)
        {
            throw std::runtime_error("kh02_movie_fixture_requires_story_context");
        }
        if (auto* collapse = cutscenes.collapse_actor();
            is_uobject_valid_fast(collapse) && flag(collapse, L"bIsPlaying"))
        {
            throw std::runtime_error("kh02_movie_fixture_during_collapse");
        }
        auto* library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        if (action == Action::FlowMoviePrepare || action == Action::FlowMoviePrepareStory ||
            action == Action::FlowMovieControl)
        {
            if (movie_fixture)
            {
                throw std::runtime_error("kh02_movie_fixture_already_active");
            }
            const auto* filename =
                action == Action::FlowMovieControl ? L"main/ca406_shot010.usm" : L"main/Rebellis02/gear_a.usm";
            if (action == Action::FlowMoviePrepare || action == Action::FlowMoviePrepareStory)
            {
                movie_bridge.prepare(pawn, nullptr,
                                     action == Action::FlowMoviePrepareStory ? L"Rebellis02/ja/cs_dw026_mv.usm"
                                                                             : L"Rebellis02/gear_a.usm",
                                     false);
            }
            else
            {
                ReflectedCall prepare(library, L"TresUI_PrepareVideo");
                prepare.string(L"MovieFilePath", filename);
                prepare.boolean(L"PauseWhenPrepared", true);
                prepare.boolean(L"AlphaComposite", false);
                prepare.integer(L"SlotNo", 0);
                prepare.run();
            }
            movie_fixture = true;
            native_movie_fixture = action == Action::FlowMovieControl;
            // Source movies use the shared-frame bridge's own history.
            // Only the native-player control fixture needs backend polling.
            native_movie.begin(native_movie_fixture);
            movie_calls.push_back({{"fixture", true}, {"native_movie", to_utf8(filename)}});
        }
        else
        {
            if (!movie_fixture)
            {
                throw std::runtime_error("kh02_movie_fixture_not_active");
            }
            if (!native_movie_fixture)
            {
                if (action == Action::FlowMovieResume)
                {
                    movie_bridge.resume();
                }
                else
                {
                    movie_bridge.stop();
                }
            }
            else
            {
                ReflectedCall call(library,
                                   action == Action::FlowMovieResume ? L"TresUI_ResumeVideo" : L"TresUI_StopVideo");
                if (action == Action::FlowMovieResume)
                {
                    call.integer(L"SlotNo", 0);
                }
                call.run();
            }
            if (action == Action::FlowMovieStop)
            {
                const bool observe_final = native_movie.observing();
                ReflectedCall release(library, L"TresUI_ReleaseVideoSlot");
                release.integer(L"SlotNo", 0);
                release.run();
                if (observe_final)
                {
                    native_movie.observe(gameplay.world.Get(), true);
                }
                movie_fixture = false;
                native_movie_fixture = false;
            }
        }
    }

} // namespace kh3ap
