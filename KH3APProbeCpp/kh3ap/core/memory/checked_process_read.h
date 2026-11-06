#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace kh3ap
{
    // ReadProcessMemory checks the entire source range and reports inaccessible
    // or partial reads. A separate VirtualQuery before this read duplicates the
    // kernel work and cannot guarantee that the mapping remains valid anyway.
    // This is a fresh read, not a cache or permission to call through a pointer.
    template <typename T> auto try_read_current_process_value(uintptr_t address) -> std::optional<T>
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (address == 0 || address > (std::numeric_limits<uintptr_t>::max)() - (sizeof(T) - 1))
        {
            return std::nullopt;
        }
        T value{};
        SIZE_T bytes_read{};
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                               &value, sizeof(value), &bytes_read) || bytes_read != sizeof(value))
        {
            return std::nullopt;
        }
        return value;
    }
} // namespace kh3ap
