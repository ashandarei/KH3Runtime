#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

namespace kh3ap::aqua_style_unlock
{
    inline constexpr std::wstring_view token = L"KH3AP_CONTROLLER_UNLOCK::spellweaver";
    inline std::atomic<bool> received{};
    inline std::atomic<uint64_t> revision{};

    inline auto owned() -> bool
    {
        return received.load(std::memory_order_acquire);
    }

    inline void invalidate()
    {
        revision.fetch_add(1, std::memory_order_release);
    }

    // Called by the existing seed-bound AP receipt event, never a file poll.
    inline void publish(bool next)
    {
        if (received.exchange(next, std::memory_order_acq_rel) != next)
        {
            invalidate();
        }
    }
}
