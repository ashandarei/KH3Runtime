#pragma once

#include <atomic>
#include <cstdint>

namespace kh3ap
{
    // Broadcast from the existing native level-write observer. Consumers keep
    // independent revisions and read their current pawn's authoritative level.
    inline std::atomic<uint64_t> character_level_revision{};
    // Published after the shared lane's native EXP write, even when no Sora
    // pawn exists to emit a level-parameter write.
    inline std::atomic<uint64_t> shared_experience_revision{};
} // namespace kh3ap
