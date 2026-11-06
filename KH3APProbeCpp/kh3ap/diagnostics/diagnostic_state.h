#pragma once

#include "kh3ap/diagnostics/output/diagnostic_once_set.h"

namespace kh3ap
{
    class RuntimeDiagnosticState
    {
    public:
        auto ap_message_signature_candidates() -> DiagnosticOnceSet&;
        auto runtime_grant_signature_paths() -> DiagnosticOnceSet&;
    private:
        DiagnosticOnceSet ap_message_signature_candidates_{};
        DiagnosticOnceSet runtime_grant_signature_paths_{};
    };
}
