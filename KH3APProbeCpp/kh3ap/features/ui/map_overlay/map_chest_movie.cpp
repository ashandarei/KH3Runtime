#include "kh3ap/features/ui/map_overlay/map_chest_movie.h"
#include "kh3ap/core/ui/embedded_movie_resource.h"
#include "kh3ap/core/performance_build.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/core/reflection/reflection_core.h"
#endif

#include <stdexcept>

namespace kh3ap {
auto map_chest_movie_bytes() -> std::span<const uint8_t> {
    // The resource stays mapped with this DLL. Resolve it once, with no disk
    // reads, decompression, script compilation or per-frame resource lookups.
    static const auto movie = []() -> std::span<const uint8_t> {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const RuntimeHotPathProfileScope profile{L"map_overlay:movie_resource_lookup"};
#endif
        const auto bytes = load_embedded_movie_resource(L"KH3AP_MAP_CHEST_MOVIE", map_chest_movie_size);
        if (bytes.empty()) throw std::runtime_error("map_movie_resource_missing_or_invalid");
        return bytes;
    }();
    return movie;
}
}
