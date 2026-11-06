#pragma once

#include <atomic>
#include <cstdint>
#include "kh3ap/core/events/world_context_events.h"

namespace kh3ap
{
    enum class MusicUpdate : uint32_t
    {
        Startup = 1,
        Settings = 2,
        Context = 4,
        Menu = 8,
        Form = 16,
        Decoded = 32,
        Volume = 64,
        Objects = 128
    };

    // Producers publish reasons, never objects. The game thread drains once;
    // quiet frames do not inspect settings, levels, slots or decoder tasks.
    inline std::atomic<uint32_t> music_update_events{static_cast<uint32_t>(MusicUpdate::Startup)};
    inline auto request_music_update(MusicUpdate reason) noexcept -> void
    {
        // These native map/controller producers also run with music disabled.
        // Broadcast context to Gummi colors independently of the music mailbox.
        if (reason == MusicUpdate::Context) world_context_revision.fetch_add(1, std::memory_order_release);
        music_update_events.fetch_or(static_cast<uint32_t>(reason), std::memory_order_release);
    }
    inline auto take_music_updates() noexcept -> uint32_t
    { return music_update_events.exchange(0, std::memory_order_acquire); }
}
