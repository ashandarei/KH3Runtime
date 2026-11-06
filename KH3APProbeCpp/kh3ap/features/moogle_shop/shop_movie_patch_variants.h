#pragma once
#include "kh3ap/features/moogle_shop/shop_movie_patch.h"
#include "kh3ap/features/moogle_shop/shop_movie_patch_data.h"
#include "kh3ap/features/moogle_shop/shop_movie_patch_steam_data.h"
#include <string_view>

namespace kh3ap {
// Match the exact loaded movie, rather than assuming storefront code/assets
// are identical. Unknown or modified movies still fail closed.
inline auto decode_supported_shop_movie(std::span<const uint8_t> original,
        std::vector<uint8_t>& result) -> std::string_view {
    namespace egs=shop_movie_patch;
    namespace steam=shop_movie_patch_steam;
    if(original.size()==egs::original_size && decode_shop_movie_patch(original,egs::delta_bytes(),egs::original_size,egs::original_hash,
            egs::patched_size,egs::patched_hash,result))return "egs";
    if(original.size()==steam::original_size && decode_shop_movie_patch(original,steam::delta_bytes(),steam::original_size,steam::original_hash,
            steam::patched_size,steam::patched_hash,result))return "steam";
    return {};
}
}
