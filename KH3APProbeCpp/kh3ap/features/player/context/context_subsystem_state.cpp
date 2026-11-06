#include "kh3ap/features/player/context/context_subsystem_state.h"

namespace kh3ap
{
    auto RuntimeContextSubsystemState::context_state() -> RuntimeContextState&
    {
        return context_state_;
    }
}
