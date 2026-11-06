#pragma once
#include <cstdint>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::kh02rewards
{
    // Presentation only. The caller has already committed inventory ownership;
    // the HUD owner is borrowed from the current controller's native resolver.
    auto present_inventory_item(RC::Unreal::UObject* aqua, RC::Unreal::UObject* hud_owner, int32_t item_id,
                                bool inventory_full) -> bool;
} // namespace kh3ap::kh02rewards
