#pragma once
#include "kh3ap/features/player/combat/magic_command_ids.h"
#include <span>

namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::character_shortcuts
{
    bool initialize();
    void reset();
    void bind(RC::Unreal::UObject* pawn, const magic_command::Commands& spells,
              std::span<uint8_t> hud_slots);
} // namespace kh3ap::character_shortcuts
