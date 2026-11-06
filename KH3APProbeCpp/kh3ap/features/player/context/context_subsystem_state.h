#pragma once

#include "kh3ap/features/player/context/context_state.h"

namespace kh3ap
{
    class RuntimeContextSubsystemState
    {
    public:
        auto context_state() -> RuntimeContextState&;

    private:
        RuntimeContextState context_state_{};
    };
}
