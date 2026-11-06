#include "kh3ap/features/treasure/treasure_runtime_state.h"

#include <string>

namespace kh3ap
{
    auto TreasureRuntimeState::treasure_state_retry_queue() -> TreasureStateRetryQueue&
    {
        return treasure_state_retry_queue_;
    }

    auto TreasureRuntimeState::treasure_logic_actor_retry_queue() -> TreasureLogicActorRetryQueue&
    {
        return treasure_logic_actor_retry_queue_;
    }

    auto TreasureRuntimeState::class_cache() -> TreasureClassCache&
    {
        return class_cache_;
    }

    auto TreasureRuntimeState::should_dump_property_diagnostics(uintptr_t object_key) -> bool
    {
        return property_dumps_enabled() &&
               logged_property_diagnostics_.check_and_remember(std::to_wstring(object_key));
    }
}
