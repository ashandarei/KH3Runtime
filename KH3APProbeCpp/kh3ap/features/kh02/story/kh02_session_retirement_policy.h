#pragma once
#include <array>
#include <exception>
#include <span>
#include <string>

namespace kh3ap
{
    enum class Kh02RetirementReason
    {
        SourceReplacement,
        Failure,
        WorldDeparture
    };

    enum class Kh02RetirementAction
    {
        DeactivateEnemies,
        ObserveDestination,
        CancelArrival,
        InvalidateSource,
        RetireMovie,
        CancelCutscenes,
        ClearHelp,
        ClearInformation,
        CancelTravel,
        CancelPostbossExit,
        ClearForestHandoff,
        ClearDarksideProjectiles,
        DisarmCastle,
        RetireEntry,
        UnbindChallenges,
        HideHud,
        CloseChallenges,
        ReleaseWardrobe,
        RestoreHud,
        CancelInputs,
        ClearWorldBindings,
        RestoreMeshBuffers,
        ReleaseEntryAssets
    };

    enum class Kh02RetirementFailurePolicy
    {
        Propagate,
        IgnoreAll,
        CollectStandardException
    };

    struct Kh02RetirementStep
    {
        Kh02RetirementAction action;
        Kh02RetirementFailurePolicy failure = Kh02RetirementFailurePolicy::Propagate;
    };

    inline auto kh02_retirement_plan(Kh02RetirementReason reason) -> std::span<const Kh02RetirementStep>
    {
        using Action = Kh02RetirementAction;
        using Failure = Kh02RetirementFailurePolicy;
        static constexpr std::array replacement{
            Kh02RetirementStep{Action::RetireMovie},        Kh02RetirementStep{Action::CancelCutscenes},
            Kh02RetirementStep{Action::ClearHelp},          Kh02RetirementStep{Action::ClearInformation},
            Kh02RetirementStep{Action::ClearForestHandoff}, Kh02RetirementStep{Action::ClearDarksideProjectiles},
            Kh02RetirementStep{Action::CancelTravel}};
        static constexpr std::array failed{Kh02RetirementStep{Action::CancelArrival, Failure::IgnoreAll},
                                           Kh02RetirementStep{Action::InvalidateSource, Failure::IgnoreAll},
                                           Kh02RetirementStep{Action::RetireMovie, Failure::IgnoreAll},
                                           Kh02RetirementStep{Action::CancelCutscenes, Failure::IgnoreAll},
                                           Kh02RetirementStep{Action::ClearHelp},
                                           Kh02RetirementStep{Action::ClearInformation},
                                           Kh02RetirementStep{Action::CancelTravel},
                                           Kh02RetirementStep{Action::CancelPostbossExit},
                                           Kh02RetirementStep{Action::ClearForestHandoff},
                                           Kh02RetirementStep{Action::ClearDarksideProjectiles},
                                           Kh02RetirementStep{Action::DisarmCastle},
                                           Kh02RetirementStep{Action::RetireEntry},
                                           Kh02RetirementStep{Action::CloseChallenges, Failure::IgnoreAll},
                                           Kh02RetirementStep{Action::ReleaseWardrobe, Failure::IgnoreAll},
                                           Kh02RetirementStep{Action::RestoreHud, Failure::IgnoreAll},
                                           Kh02RetirementStep{Action::CancelInputs, Failure::IgnoreAll}};
        static constexpr std::array departure{
            Kh02RetirementStep{Action::DeactivateEnemies},
            Kh02RetirementStep{Action::ObserveDestination, Failure::CollectStandardException},
            Kh02RetirementStep{Action::CancelArrival, Failure::CollectStandardException},
            Kh02RetirementStep{Action::RetireMovie, Failure::CollectStandardException},
            Kh02RetirementStep{Action::CancelCutscenes, Failure::CollectStandardException},
            Kh02RetirementStep{Action::UnbindChallenges, Failure::CollectStandardException},
            Kh02RetirementStep{Action::HideHud, Failure::CollectStandardException},
            Kh02RetirementStep{Action::CloseChallenges, Failure::CollectStandardException},
            Kh02RetirementStep{Action::ReleaseWardrobe, Failure::CollectStandardException},
            Kh02RetirementStep{Action::RestoreHud, Failure::CollectStandardException},
            Kh02RetirementStep{Action::CancelInputs, Failure::CollectStandardException},
            Kh02RetirementStep{Action::ClearHelp},
            Kh02RetirementStep{Action::ClearInformation},
            Kh02RetirementStep{Action::ClearDarksideProjectiles},
            Kh02RetirementStep{Action::ClearWorldBindings},
            Kh02RetirementStep{Action::RestoreMeshBuffers, Failure::CollectStandardException},
            Kh02RetirementStep{Action::ReleaseEntryAssets, Failure::CollectStandardException}};
        switch (reason)
        {
        case Kh02RetirementReason::SourceReplacement:
            return replacement;
        case Kh02RetirementReason::Failure:
            return failed;
        case Kh02RetirementReason::WorldDeparture:
            return departure;
        }
        return {};
    }

    // The original failure remains authoritative on recovery. Departure retains
    // its first standard exception while attempting later independent releases;
    // source replacement propagates immediately to its caller. Reentrant source
    // retirement uses only local iteration/error state.
    template <class Execute> auto kh02_run_retirement(Kh02RetirementReason reason, Execute&& execute) -> std::string
    {
        std::string first_error;
        for (const auto step : kh02_retirement_plan(reason))
        {
            if (step.failure == Kh02RetirementFailurePolicy::Propagate)
            {
                execute(step.action);
                continue;
            }
            try
            {
                execute(step.action);
            }
            catch (const std::exception& error)
            {
                if (step.failure == Kh02RetirementFailurePolicy::CollectStandardException && first_error.empty())
                {
                    first_error = error.what();
                }
            }
            catch (...)
            {
                if (step.failure != Kh02RetirementFailurePolicy::IgnoreAll)
                {
                    throw;
                }
            }
        }
        return first_error;
    }
} // namespace kh3ap
