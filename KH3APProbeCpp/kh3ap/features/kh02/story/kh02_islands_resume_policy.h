#pragma once
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"

namespace kh3ap
{
    constexpr int kh02_islands_entry_checkpoint(int checkpoint)
    {
        // DW027 writes 500 then 510 before playback. Re-entry at either value
        // skips its load branch, but has no live Matinee to reach DW028/520.
        // Replay the authored intro from its preceding completed chapter.
        if (checkpoint >= kh02_checkpoint::island_scene_dw027 && checkpoint < kh02_checkpoint::demon_tide_start)
        {
            return kh02_checkpoint::forest_scene_dw026;
        }
        return checkpoint;
    }
} // namespace kh3ap
