#pragma once

#include <cstdint>
#include <vector>

namespace RC::Unreal { class UObject; }

namespace kh3ap::music_objects
{
    // Game-thread bootstrap only. One initial enumeration covers objects that
    // predate the shared constructor observer; transitions never enumerate.
    auto initialize() -> void;
    // Shared UObjectArray create listener, possibly on a loader thread.
    // Queued pointers are protected by the deletion listener until converted
    // into native weak references on the game thread.
    auto constructed(RC::Unreal::UObject* object) -> void;

    struct Snapshot
    {
        std::vector<RC::Unreal::UObject*> settings, load_assets, managers;
        bool unfinished{};
        uint64_t bootstrap_scans{}, generation{};
    };
    // Game thread, returned pointers valid only for the current synchronous call.
    auto snapshot() -> Snapshot;
}
