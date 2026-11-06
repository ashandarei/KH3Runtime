#pragma once
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/player/characters/character_entries.h"
#include <initializer_list>

namespace kh3ap::character_native
{
    using Build = native_build::Build;

    constexpr Build identify(uint32_t timestamp, uint32_t image_size)
    {
        return native_build::identify(timestamp, image_size);
    }

    constexpr uintptr_t rva(Site site, Build image)
    {
        const auto& entry = entries[static_cast<size_t>(site)];
        return image == Build::Egs ? entry.egs_rva : image == Build::Steam ? entry.steam_rva : 0;
    }

    Build build();
    uintptr_t rva(Site site);
    // Verify each site before installing its hook or calling it. Successful
    // checks are retained because our own detours replace those entry bytes.
    bool verify_sites(std::initializer_list<Site> sites);
}
