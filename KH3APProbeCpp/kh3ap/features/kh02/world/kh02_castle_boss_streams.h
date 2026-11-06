#pragma once
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    inline auto kh02_castle_boss_streams_required(int checkpoint, int gear_count) -> bool
    {
        // The first trigger needs the same camera and encounter listeners as
        // Retry. Loading those listeners does not start the battle; the source
        // overlap event still owns the introduction and checkpoint advance.
        return checkpoint >= kh02_checkpoint::fourth_gear && checkpoint <= kh02_checkpoint::castle_tower_start &&
               gear_count == kh02_gear_counter::all_collected;
    }

    class Kh02StoryFlags;
    auto kh02_prepare_castle_boss_streams(RC::Unreal::UObject* world, const Kh02StoryFlags& flags,
                                          nlohmann::json& proof) -> bool;
} // namespace kh3ap
