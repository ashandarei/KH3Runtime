#pragma once

#include "kh3ap/diagnostics/deferred_event_state_resample_queue.h"
#include "kh3ap/features/grants/events/event_array_snapshot_store.h"

namespace kh3ap
{
    class RuntimeEventState
    {
    public:
        auto event_array_snapshot_store() -> EventArraySnapshotStore&;
        auto deferred_resample_queue() -> DeferredEventStateResampleQueue&;

    private:
        EventArraySnapshotStore event_array_snapshot_store_{};
        DeferredEventStateResampleQueue deferred_resample_queue_{};
    };
}
