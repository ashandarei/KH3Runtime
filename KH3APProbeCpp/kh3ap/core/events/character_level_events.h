#pragma once

#include <atomic>
#include <cstdint>

namespace kh3ap
{
    // Broadcast from the existing native level-write observer. Consumers keep
    // independent revisions and read their current pawn's authoritative level.
    inline std::atomic<uint64_t> character_level_revision{};
} // namespace kh3ap
