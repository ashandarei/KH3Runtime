#include "kh3ap/features/player/characters/character_profile.h"
#include <cassert>

int main()
{
    using namespace kh3ap::character_native;
    assert(identify(0x60547d11, 0xaebf000) == Build::Egs);
    assert(identify(0x66743f34, 0xa969000) == Build::Steam);
    // Never combine one storefront's timestamp with the other's image layout.
    assert(identify(0x60547d11, 0xa969000) == Build::Unsupported);
    assert(identify(0x66743f34, 0xaebf000) == Build::Unsupported);
    assert(identify(0, 0) == Build::Unsupported);
    // A wider caller must not truncate an unsupported image size into a
    // supported fingerprint before applying feature-specific validation.
    assert(kh3ap::native_build::identify(0x60547d11, 0x1aebf000ULL) == Build::Unsupported);
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto site = static_cast<Site>(index);
        assert(rva(site, Build::Unsupported) == 0);
        const auto& entry = entries[index];
        assert((entry.egs_rva != 0) == !entry.egs_bytes.empty());
        assert((entry.steam_rva != 0) == !entry.steam_bytes.empty());
        if (site != Site::camp_roster && site != Site::camp_save_bank)
        {
            assert(rva(site, Build::Egs) && rva(site, Build::Steam));
        }
    }
    // The Steam camp adapter hooks a complete reset function, never the old
    // audit's interior block. Spawn callbacks also have different ABIs.
    assert(rva(Site::camp_roster, Build::Steam) == 0);
    assert(rva(Site::camp_roster_reset, Build::Steam) != 0x154c842);
    assert(rva(Site::pool_spawn, Build::Steam) == 0x41b9170);
    assert(rva(Site::pool_spawn, Build::Egs) == 0x472a3d0);
}
