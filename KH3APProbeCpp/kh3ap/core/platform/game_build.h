#pragma once

#include <cstdint>

namespace kh3ap::native_build
{
    enum class Build { Unsupported, Egs, Steam };

    struct Fingerprint
    {
        uint32_t timestamp;
        uint32_t image_size;

        constexpr bool matches(uint32_t candidate_timestamp, uint64_t candidate_size) const
        {
            return timestamp == candidate_timestamp && image_size == candidate_size;
        }
    };

    // Verified shipping images. Identification does not replace a feature's
    // RVA, byte-signature, unwind, or reflected-layout validation.
    inline constexpr Fingerprint egs{0x60547D11, 0xAEBF000}; // CL 1065258
    inline constexpr Fingerprint steam{0x66743F34, 0xA969000};

    constexpr Build identify(uint32_t timestamp, uint64_t image_size)
    {
        if (egs.matches(timestamp, image_size))
        {
            return Build::Egs;
        }
        if (steam.matches(timestamp, image_size))
        {
            return Build::Steam;
        }
        return Build::Unsupported;
    }
}
