#pragma once

#include "kh3ap/features/grants/abilities/ability_diagnostic_state.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"
#include "kh3ap/features/player/context/context_subsystem_state.h"
#include "kh3ap/diagnostics/diagnostic_state.h"
#include "kh3ap/features/grants/events/event_state.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_cache.h"
#include "kh3ap/features/player/keyblades/keyblade_options_helpers.h"
#include "kh3ap/diagnostics/ui_diagnostic_state.h"
#include "kh3ap/features/treasure/treasure_runtime_state.h"

namespace kh3ap
{
    class RuntimeState
    {
    public:
        auto event_state() -> RuntimeEventState&;
        auto object_cache() -> RuntimeObjectCache&;
        auto grant_state() -> RuntimeGrantState&;
        auto diagnostic_state() -> RuntimeDiagnosticState&;
        auto ap_message_param_storage() -> RuntimeApMessageParamStorage&;
        auto ability_diagnostic_state() -> RuntimeAbilityDiagnosticState&;
        auto ui_diagnostic_state() -> RuntimeUiDiagnosticState&;
        auto treasure_state() -> TreasureRuntimeState&;
        auto context_subsystem_state() -> RuntimeContextSubsystemState&;
        auto keyblade_style_state() -> RuntimeKeybladeStyleState&;

    private:
        RuntimeEventState event_state_{};
        RuntimeObjectCache object_cache_{};
        RuntimeGrantState grant_state_{};
        RuntimeDiagnosticState diagnostic_state_{};
        RuntimeApMessageParamStorage ap_message_param_storage_{};
        RuntimeAbilityDiagnosticState ability_diagnostic_state_{};
        RuntimeUiDiagnosticState ui_diagnostic_state_{};
        TreasureRuntimeState treasure_state_{};
        RuntimeContextSubsystemState context_subsystem_state_{};
        RuntimeKeybladeStyleState keyblade_style_state_{};
    };
}
