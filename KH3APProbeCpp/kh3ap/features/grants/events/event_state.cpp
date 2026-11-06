#include "kh3ap/features/grants/events/event_state.h"

namespace kh3ap
{
    auto RuntimeEventState::event_array_snapshot_store() -> EventArraySnapshotStore&
    {
        return event_array_snapshot_store_;
    }

    auto RuntimeEventState::deferred_resample_queue() -> DeferredEventStateResampleQueue&
    {
        return deferred_resample_queue_;
    }
}
