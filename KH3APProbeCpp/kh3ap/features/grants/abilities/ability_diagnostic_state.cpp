#include "kh3ap/features/grants/abilities/ability_diagnostic_state.h"

namespace kh3ap
{
    auto RuntimeAbilityDiagnosticState::stable_trace_dedupe() -> StableAbilityTraceDedupe&
    {
        return stable_trace_dedupe_;
    }

    auto RuntimeAbilityDiagnosticState::probe_diagnostic_state() -> AbilityProbeDiagnosticState&
    {
        return probe_diagnostic_state_;
    }
}
