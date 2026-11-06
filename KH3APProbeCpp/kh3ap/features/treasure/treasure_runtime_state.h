#pragma once

#include "kh3ap/diagnostics/output/diagnostic_once_set.h"
#include "kh3ap/features/treasure/treasure_class_cache.h"
#include "kh3ap/features/treasure/treasure_retry_queues.h"

#include <cstddef>
#include <cstdint>

namespace kh3ap
{
    class TreasureRuntimeState
    {
    public:
        static constexpr size_t pending_treasure_probe_max = 64;
        static constexpr int open_treasure_state_retry_max = 4;
        static constexpr size_t pending_treasure_logic_actor_probe_max = 64;
        static constexpr int treasure_logic_actor_retry_max = 4;

        [[nodiscard]] static constexpr auto property_dumps_enabled() -> bool
        {
            return false;
        }

        [[nodiscard]] auto treasure_state_retry_queue() -> TreasureStateRetryQueue&;
        [[nodiscard]] auto treasure_logic_actor_retry_queue() -> TreasureLogicActorRetryQueue&;
        [[nodiscard]] auto class_cache() -> TreasureClassCache&;
        auto should_dump_property_diagnostics(uintptr_t object_key) -> bool;

    private:
        TreasureClassCache class_cache_{};
        TreasureStateRetryQueue treasure_state_retry_queue_{};
        TreasureLogicActorRetryQueue treasure_logic_actor_retry_queue_{};
        DiagnosticOnceSet logged_property_diagnostics_{};
    };
}
