#include "kh3ap/features/kh02/diagnostics/kh02_diagnostic_action.h"
#include <cassert>
#include <string>

using namespace kh3ap;

int main()
{
    using Action = Kh02DiagnosticAction;
    using Group = Kh02DiagnosticGroup;
    assert(kh02_decode_diagnostic_action(L"playtest_reset").group == Group::Reset);
    assert(kh02_decode_diagnostic_action(L"playtest_inspect").action == Action::PlaytestInspect);
    assert(kh02_decode_diagnostic_action(L"load").group == Group::Opening);
    assert(kh02_decode_diagnostic_action(L"flow_movie_prepare_story").action == Action::FlowMoviePrepareStory);
    assert(kh02_decode_diagnostic_action(L"within_replay_dw020").action == Action::WithinReplayDw020);

    // Prefix routing intentionally defers unknown commands to the native
    // context guards. A non-domain spelling must fail before those guards.
    assert(kh02_decode_diagnostic_action(L"flow_unknown").action == Action::UnknownFlow);
    assert(kh02_decode_diagnostic_action(L"flow_unknown").group == Group::Flow);
    assert(kh02_decode_diagnostic_action(L"within_unknown").action == Action::UnknownWithin);
    assert(kh02_decode_diagnostic_action(L"within_unknown").group == Group::Mirror);
    assert(kh02_decode_diagnostic_action(L"flow_castle_trigger_unknown").action == Action::UnknownCastleTrigger);
    assert(kh02_diagnostic_is_castle_trigger(kh02_decode_diagnostic_action(L"flow_castle_trigger_unknown").action));
    assert(kh02_decode_diagnostic_action(L"load_extra").group == Group::Invalid);
    assert(kh02_decode_diagnostic_action(L"playtest_unknown").group == Group::Invalid);
    assert(kh02_decode_diagnostic_action(L"FLOW_INSPECT").group == Group::Invalid);

    // Existing flow_load* requests load every stream unless a named subset
    // was selected. Preserve that behavior while routing with a typed action.
    for (const auto* name : {L"flow_load", L"flow_load_all", L"flow_load_future_name"})
    {
        const auto command = kh02_decode_diagnostic_action(name);
        assert(command.group == Group::Flow && command.action == Action::FlowLoadAll);
        assert(kh02_diagnostic_is_flow_load(command.action));
    }
    assert(kh02_decode_diagnostic_action(L"flow_load_camera").action == Action::FlowLoadCamera);
    assert(!kh02_diagnostic_is_flow_load(Action::FlowPrepare));
    assert(!kh02_diagnostic_is_castle_trigger(Action::FlowInspect));

    for (const auto* name : {L"flow_inspect", L"flow_movie_prepare_story", L"flow_movie_resume", L"flow_movie_stop"})
    {
        assert(kh02_diagnostic_allows_mirror_flow(kh02_decode_diagnostic_action(name).action));
    }
    for (const auto* name : {L"flow_movie_prepare", L"flow_movie_control", L"flow_start", L"flow_prepare",
                             L"flow_load_gameflow", L"flow_unknown"})
    {
        assert(!kh02_diagnostic_allows_mirror_flow(kh02_decode_diagnostic_action(name).action));
    }
    assert(kh02_diagnostic_is_movie(Action::FlowMovieControl));
    assert(!kh02_diagnostic_is_movie(Action::FlowCapture));

    // The decoder stores an enum value, never the caller's temporary text.
    auto command = kh02_decode_diagnostic_action(std::wstring(L"within_test_character_aqua"));
    assert(command.group == Group::Mirror && command.action == Action::WithinTestCharacterAqua);
}
