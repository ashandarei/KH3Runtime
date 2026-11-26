#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <span>

namespace kh3ap
{
    inline bool replace_checked_code_bytes(uint8_t* address, std::span<const uint8_t> expected,
                              std::span<const uint8_t> replacement)
    {
        if (expected.size() != replacement.size() || std::memcmp(address, expected.data(), expected.size()) != 0)
        {
            return false;
        }

        DWORD protection{};
        if (!VirtualProtect(address, replacement.size(), PAGE_EXECUTE_READWRITE, &protection))
        {
            return false;
        }

        std::memcpy(address, replacement.data(), replacement.size());
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, replacement.size()) != FALSE;
        if (!flushed)
        {
            std::memcpy(address, expected.data(), replacement.size());
            FlushInstructionCache(GetCurrentProcess(), address, replacement.size());
        }
        DWORD ignored{};
        VirtualProtect(address, replacement.size(), protection, &ignored);
        return flushed;
    }
}
