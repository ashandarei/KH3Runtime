#include "kh3ap/features/kh02/core/kh02_profile.h"
#include <cassert>

int main()
{
    using namespace kh3ap::kh02_native;
    assert(identify(0x60547d11, 0xaebf000) == Build::Egs);
    assert(identify(0x66743f34, 0xa969000) == Build::Steam);
    assert(identify(0x60547d11, 0xa969000) == Build::Unsupported);
    assert(identify(0x66743f34, 0xaebf000) == Build::Unsupported);
    assert(image_size(Build::Unsupported) == 0);
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const auto site = static_cast<Site>(index);
        assert(rva(site, Build::Unsupported) == 0);
        assert(rva(site, Build::Egs) > 0);
        assert(rva(site, Build::Egs) < image_size(Build::Egs));
        assert(rva(site, Build::Steam) > 0);
        assert(rva(site, Build::Steam) < image_size(Build::Steam));
    }
}
