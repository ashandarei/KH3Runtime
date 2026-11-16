#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

namespace kh3ap::kairi_style_unlock
{
    inline constexpr std::wstring_view token = L"KH3AP_CONTROLLER_UNLOCK::seven_wishes";
    inline std::atomic<bool> received{};
    inline std::atomic<uint64_t> revision{};

    inline auto owned() -> bool
    {
        return received.load(std::memory_order_acquire);
    }

    // Ownership is rebuilt by the seed-bound AP receipt event.
    inline void publish(bool next)
    {
        if (received.exchange(next, std::memory_order_acq_rel) != next)
        {
            revision.fetch_add(1, std::memory_order_release);
        }
    }
}
