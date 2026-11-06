#include "kh3ap/diagnostics/ui_diagnostic_state.h"

namespace kh3ap
{
    auto RuntimeUiDiagnosticState::heartbeat_state() -> UpdateHeartbeatState&
    {
        return heartbeat_state_;
    }

    auto RuntimeUiDiagnosticState::blocking_ui_state() -> RuntimeBlockingUiState&
    {
        return blocking_ui_state_;
    }
}
