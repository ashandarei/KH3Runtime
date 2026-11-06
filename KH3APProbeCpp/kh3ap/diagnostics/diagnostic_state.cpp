#include "kh3ap/diagnostics/diagnostic_state.h"

namespace kh3ap
{
    auto RuntimeDiagnosticState::ap_message_signature_candidates() -> DiagnosticOnceSet&
    {
        return ap_message_signature_candidates_;
    }

    auto RuntimeDiagnosticState::runtime_grant_signature_paths() -> DiagnosticOnceSet&
    {
        return runtime_grant_signature_paths_;
    }

}
