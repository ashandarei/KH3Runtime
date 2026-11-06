#include "kh3ap/app/state.h"

namespace kh3ap
{
    auto RuntimeState::event_state() -> RuntimeEventState&
    {
        return event_state_;
    }

    auto RuntimeState::object_cache() -> RuntimeObjectCache&
    {
        return object_cache_;
    }

    auto RuntimeState::grant_state() -> RuntimeGrantState&
    {
        return grant_state_;
    }

    auto RuntimeState::diagnostic_state() -> RuntimeDiagnosticState&
    {
        return diagnostic_state_;
    }

    auto RuntimeState::ap_message_param_storage() -> RuntimeApMessageParamStorage&
    {
        return ap_message_param_storage_;
    }

    auto RuntimeState::ability_diagnostic_state() -> RuntimeAbilityDiagnosticState&
    {
        return ability_diagnostic_state_;
    }

    auto RuntimeState::ui_diagnostic_state() -> RuntimeUiDiagnosticState&
    {
        return ui_diagnostic_state_;
    }

    auto RuntimeState::treasure_state() -> TreasureRuntimeState&
    {
        return treasure_state_;
    }

    auto RuntimeState::context_subsystem_state() -> RuntimeContextSubsystemState&
    {
        return context_subsystem_state_;
    }

    auto RuntimeState::keyblade_style_state() -> RuntimeKeybladeStyleState&
    {
        return keyblade_style_state_;
    }
}
