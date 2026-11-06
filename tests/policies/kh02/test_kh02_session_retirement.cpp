#include "kh3ap/features/kh02/story/kh02_session_retirement_policy.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kh3ap;
using Action = Kh02RetirementAction;
using Reason = Kh02RetirementReason;

int main()
{
    // Capture the three original cleanup contracts, including their different
    // ordering of pending travel and forest handoff retirement.
    const std::vector replacement{
        Action::RetireMovie,        Action::CancelCutscenes,          Action::ClearHelp,   Action::ClearInformation,
        Action::ClearForestHandoff, Action::ClearDarksideProjectiles, Action::CancelTravel};
    const std::vector failed{
        Action::CancelArrival,      Action::InvalidateSource,         Action::RetireMovie,  Action::CancelCutscenes,
        Action::ClearHelp,          Action::ClearInformation,         Action::CancelTravel, Action::CancelPostbossExit,
        Action::ClearForestHandoff, Action::ClearDarksideProjectiles, Action::DisarmCastle, Action::RetireEntry,
        Action::CloseChallenges,    Action::ReleaseWardrobe,          Action::RestoreHud,   Action::CancelInputs};
    const std::vector departure{Action::DeactivateEnemies,
                                Action::ObserveDestination,
                                Action::CancelArrival,
                                Action::RetireMovie,
                                Action::CancelCutscenes,
                                Action::UnbindChallenges,
                                Action::HideHud,
                                Action::CloseChallenges,
                                Action::ReleaseWardrobe,
                                Action::RestoreHud,
                                Action::CancelInputs,
                                Action::ClearHelp,
                                Action::ClearInformation,
                                Action::ClearDarksideProjectiles,
                                Action::ClearWorldBindings,
                                Action::RestoreMeshBuffers,
                                Action::ReleaseEntryAssets};
    for (const auto reason : {Reason::SourceReplacement, Reason::Failure, Reason::WorldDeparture})
    {
        std::vector<Action> trace;
        assert(kh02_run_retirement(reason, [&](Action action) { trace.push_back(action); }).empty());
        const auto& expected = reason == Reason::SourceReplacement ? replacement
                               : reason == Reason::Failure         ? failed
                                                                   : departure;
        assert(trace == expected);
    }

    std::vector<Action> trace;
    const auto departure_error = kh02_run_retirement(Reason::WorldDeparture,
                                                     [&](Action action)
                                                     {
                                                         trace.push_back(action);
                                                         if (action == Action::ObserveDestination)
                                                         {
                                                             throw std::runtime_error("first_release_failure");
                                                         }
                                                         if (action == Action::RestoreMeshBuffers)
                                                         {
                                                             throw std::runtime_error("later_release_failure");
                                                         }
                                                     });
    assert(departure_error == "first_release_failure");
    assert(trace == departure);

    // Failure recovery must attempt every independent release, including a
    // non-standard exception, while keeping the initiating session error.
    trace.clear();
    const auto recovery_error =
        kh02_run_retirement(Reason::Failure,
                            [&](Action action)
                            {
                                trace.push_back(action);
                                if (action == Action::InvalidateSource || action == Action::ReleaseWardrobe)
                                {
                                    throw std::runtime_error("cleanup_failure");
                                }
                                if (action == Action::CloseChallenges)
                                {
                                    throw 17;
                                }
                            });
    assert(recovery_error.empty());
    assert(trace == failed);

    // Replacement preserves fail-fast behavior; departure never swallows an
    // unknown exception. Propagating actions remain propagating in all modes.
    for (const auto reason : {Reason::SourceReplacement, Reason::Failure, Reason::WorldDeparture})
    {
        const auto stop = reason == Reason::SourceReplacement ? Action::CancelCutscenes : Action::ClearHelp;
        trace.clear();
        try
        {
            kh02_run_retirement(reason,
                                [&](Action action)
                                {
                                    trace.push_back(action);
                                    if (action == stop)
                                    {
                                        throw std::runtime_error("propagate");
                                    }
                                });
            assert(false);
        }
        catch (const std::runtime_error& error)
        {
            assert(std::string(error.what()) == "propagate");
        }
        assert(trace.back() == stop);
    }
    trace.clear();
    try
    {
        kh02_run_retirement(Reason::WorldDeparture,
                            [&](Action action)
                            {
                                trace.push_back(action);
                                if (action == Action::ObserveDestination)
                                {
                                    throw 23;
                                }
                            });
        assert(false);
    }
    catch (int error)
    {
        assert(error == 23);
    }
    assert(trace.back() == Action::ObserveDestination);

    // Source invalidation/world observation may synchronously retire their
    // presentation. Nested plans must not disturb the outer iteration or its
    // first recorded error.
    trace.clear();
    bool old_generation_allowed = true;
    int nested_calls{};
    const auto nested_error = kh02_run_retirement(Reason::WorldDeparture,
                                                  [&](Action action)
                                                  {
                                                      trace.push_back(action);
                                                      if (action == Action::ObserveDestination)
                                                      {
                                                          old_generation_allowed = false;
                                                          kh02_run_retirement(Reason::SourceReplacement,
                                                                              [&](Action)
                                                                              {
                                                                                  assert(!old_generation_allowed);
                                                                                  ++nested_calls;
                                                                              });
                                                          throw std::runtime_error("outer_error");
                                                      }
                                                  });
    assert(nested_error == "outer_error");
    assert(nested_calls == static_cast<int>(replacement.size()));
    assert(trace == departure);
}
