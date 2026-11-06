#pragma once
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/music/events/sead_music_events.h"

namespace kh3ap::sead_music_detail
{
    struct Site { uintptr_t rva; std::array<uint8_t, 32> signature; };
#include "kh3ap/features/music/events/sead_music_event_sites.inl"

    struct Profile
    {
        uint32_t timestamp, image_size;
        const wchar_t* storefront;
        uintptr_t finish_owner;
        const std::array<Site, sead_music_event_count>& sites;
    };
    inline constexpr std::array<Profile, 2> profiles{{
        {kh3ap::native_build::egs.timestamp, kh3ap::native_build::egs.image_size, L"egs", 0x5802020, egs_sites},
        {kh3ap::native_build::steam.timestamp, kh3ap::native_build::steam.image_size, L"steam", 0x58246C0, steam_sites},
    }};

    // No best match or polling fallback: each supported image must also pass
    // every runtime byte signature and unwind check before hooks are installed.
    constexpr auto select_profile(uint32_t timestamp, uint32_t image_size) -> const Profile*
    {
        for (const auto& profile : profiles)
            if (profile.timestamp == timestamp && profile.image_size == image_size) return &profile;
        return nullptr;
    }
}
