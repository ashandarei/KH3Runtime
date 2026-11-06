#pragma once
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"
#include <algorithm>
#include <stdexcept>

namespace kh3ap
{
    struct Kh02PostBossHandoffState
    {
        bool observed{}, requested{};
        float maximum_position{};

        void observe_playing(bool playing)
        {
            observed |= playing;
        }

        void observe_position(float position)
        {
            maximum_position = std::max(maximum_position, position);
        }

        void clear_observation()
        {
            observed = false;
            maximum_position = 0;
        }

        void request_exit(int checkpoint, int gears)
        {
            if (checkpoint < kh02_checkpoint::gear_scene_dw013 || gears != kh02_gear_counter::all_collected ||
                requested || !observed)
            {
                throw std::runtime_error("kh02_travel_before_boss_scene");
            }
            requested = true;
        }

        auto take_request(bool owns_world) -> bool
        {
            if (!requested || !owns_world)
            {
                return false;
            }
            requested = false;
            return true;
        }

        void cancel_exit()
        {
            requested = false;
        }
    };
} // namespace kh3ap
