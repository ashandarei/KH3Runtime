#pragma once

#include "kh3ap/diagnostics/ability_probe/ability_probe_diagnostic_state.h"
#include "kh3ap/diagnostics/ability_probe/stable_ability_trace_dedupe.h"

namespace kh3ap
{
    class RuntimeAbilityDiagnosticState
    {
    public:
        auto stable_trace_dedupe() -> StableAbilityTraceDedupe&;
        auto probe_diagnostic_state() -> AbilityProbeDiagnosticState&;

    private:
        StableAbilityTraceDedupe stable_trace_dedupe_{};
        AbilityProbeDiagnosticState probe_diagnostic_state_{};
    };
}
