#pragma once
#include <cstdint>

namespace kh3ap
{
    // Original lookup from our verified, installed AP resolver detour only.
    // Does not accept or follow an arbitrary replacement function.
    auto original_item_catalog_lookup(uintptr_t expected_target) -> uintptr_t;
}
