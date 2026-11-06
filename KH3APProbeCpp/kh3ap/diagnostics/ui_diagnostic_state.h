#pragma once

#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"

namespace kh3ap
{
    class RuntimeUiDiagnosticState
    {
    public:
        auto heartbeat_state() -> UpdateHeartbeatState&;
        auto blocking_ui_state() -> RuntimeBlockingUiState&;

    private:
        UpdateHeartbeatState heartbeat_state_{};
        RuntimeBlockingUiState blocking_ui_state_{};
    };
}
