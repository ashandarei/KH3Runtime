#pragma once
#include "kh3ap/features/player/player_ids.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_chest_voice
{
    // Selection preloads the native localized sound alongside the character.
    const wchar_t* asset_path(PlayerId player);
    bool prepare(RC::Unreal::UObject* pawn, RC::Unreal::UObject* instance,
                 RC::Unreal::UObject* large_chest_animation);
    RC::Unreal::UObject* replacement_voice(RC::Unreal::UObject* notify, RC::Unreal::UObject* mesh,
                                           RC::Unreal::UObject* animation);
} // namespace kh3ap::character_chest_voice
