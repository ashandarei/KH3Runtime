#pragma once
#include "kh3ap/core/platform/game_build.h"
#include "kh02_entries.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include <initializer_list>

namespace kh3ap::kh02_native
{
    using character_native::Build;
    using character_native::build;
    using character_native::identify;

    constexpr auto rva(Site site, Build image) -> uintptr_t
    {
        const auto& entry = entries[static_cast<size_t>(site)];
        if (image == Build::Egs)
        {
            return entry.egs_rva;
        }
        if (image == Build::Steam)
        {
            return entry.steam_rva;
        }
        return 0;
    }

    inline auto rva(Site site) -> uintptr_t
    {
        return rva(site, build());
    }

    constexpr auto image_size(Build image) -> uintptr_t
    {
        return image == Build::Egs ? native_build::egs.image_size :
               image == Build::Steam ? native_build::steam.image_size : 0;
    }

    inline auto image_size() -> uintptr_t
    {
        return image_size(build());
    }

    // Cache verified code before our own detours replace entry instructions.
    // Data addresses are checked through the guarded owning code and live owner.
    auto verify_sites(std::initializer_list<Site> sites) -> bool;
} // namespace kh3ap::kh02_native
