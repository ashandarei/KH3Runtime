#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap::titan_objects
{
    // Game-thread bootstrap only. One initial enumeration covers objects that
    // predate the shared constructor observer; transitions never enumerate.
    auto initialize() -> void;
    // Startup configuration, before initialize enables the constructor observer.
    auto watch_pawn_class(std::size_t index, std::wstring_view path) -> void;
    // Shared UObjectArray create listener, possibly on a loader thread.
    // Queued pointers are protected by the deletion listener until converted
    // into native weak references on the game thread.
    auto constructed(RC::Unreal::UObject* object) -> void;

    struct Snapshot
    {
        std::vector<RC::Unreal::UObject*> objects, pawns;
        bool unfinished{};
        uint64_t bootstrap_scans{}, generation{};
    };
    // Game thread, returned pointers valid only for the current synchronous call.
    auto snapshot() -> Snapshot;
    auto generation() -> uint64_t;
}
