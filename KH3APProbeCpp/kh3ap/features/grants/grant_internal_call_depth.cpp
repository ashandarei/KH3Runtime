#include "kh3ap/features/grants/grant_internal_call_depth.h"

namespace kh3ap
{
    thread_local int RuntimeGrantInternalCallDepth::depth_ = 0;

    RuntimeGrantInternalCallDepth::Scope::Scope()
    {
        RuntimeGrantInternalCallDepth::enter();
    }

    RuntimeGrantInternalCallDepth::Scope::~Scope()
    {
        RuntimeGrantInternalCallDepth::leave();
    }

    auto RuntimeGrantInternalCallDepth::is_active() -> bool
    {
        return depth_ > 0;
    }

    auto RuntimeGrantInternalCallDepth::enter() -> void
    {
        ++depth_;
    }

    auto RuntimeGrantInternalCallDepth::leave() -> void
    {
        if (depth_ > 0)
        {
            --depth_;
        }
    }
}
