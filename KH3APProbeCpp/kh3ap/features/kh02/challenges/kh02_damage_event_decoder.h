#pragma once
#include "kh02_damage_event_metadata.h"
#include <optional>

namespace kh3ap
{
    // Borrow the event only for this call. Return a value copy before native damage can retire it.
    auto kh02_decode_damage_event(const void* event) -> std::optional<Kh02DamageEventMetadata>;
} // namespace kh3ap
