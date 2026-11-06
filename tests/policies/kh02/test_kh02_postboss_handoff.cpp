#include "kh3ap/features/kh02/story/kh02_postboss_handoff_state.h"
#include <cassert>

int main()
{
    using kh3ap::Kh02PostBossHandoffState;
    Kh02PostBossHandoffState state;
    const auto rejects = [&](int checkpoint, int gears)
    {
        bool rejected = false;
        try
        {
            state.request_exit(checkpoint, gears);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        assert(rejected);
        assert(!state.requested);
    };
    rejects(kh3ap::kh02_checkpoint::gear_scene_dw013, kh3ap::kh02_gear_counter::all_collected);
    state.observe_playing(true);
    state.observe_position(12.5f);
    state.observe_playing(false);
    state.observe_position(2.f);
    assert(state.observed && state.maximum_position == 12.5f);
    rejects(kh3ap::kh02_checkpoint::castle_tower_start, kh3ap::kh02_gear_counter::all_collected);
    rejects(kh3ap::kh02_checkpoint::gear_scene_dw013, 0);
    state.request_exit(kh3ap::kh02_checkpoint::gear_scene_dw013, kh3ap::kh02_gear_counter::all_collected);
    assert(!state.take_request(false));
    assert(state.requested);
    bool duplicate_rejected = false;
    try
    {
        state.request_exit(kh3ap::kh02_checkpoint::gear_scene_dw013, kh3ap::kh02_gear_counter::all_collected);
    }
    catch (const std::runtime_error&)
    {
        duplicate_rejected = true;
    }
    assert(duplicate_rejected);
    assert(state.take_request(true));
    assert(!state.requested && !state.take_request(true));
    state.request_exit(kh3ap::kh02_checkpoint::gear_scene_dw013, kh3ap::kh02_gear_counter::all_collected);
    state.clear_observation();
    assert(!state.observed && state.maximum_position == 0.f);
    assert(state.requested); // Observation reset preserves the independently pending source exit.
    state.cancel_exit();
    assert(!state.requested);
}
