#include "../../../KH3APProbeCpp/kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace kh3ap;
    using namespace kh02_progress;
    const auto encoded = [](const Flags& flags)
    {
        Json value = Json::object();
        for (const auto& [name, number] : flags)
        {
            value[name] = number;
        }
        return value;
    };
    const std::array flows{0, kh02_checkpoint::gear_scene_dw011, kh02_checkpoint::mirror_scene_dw015,
                           kh02_checkpoint::mirror_scene_dw016, std::numeric_limits<int>::max()};
    const std::array checkpoints{0, kh02_checkpoint::gear_scene_dw009, kh02_checkpoint::gear_scene_dw011,
                                 kh02_checkpoint::mirror_scene_dw015, kh02_checkpoint::mirror_scene_dw016};
    for (const auto flow : flows)
    {
        Flags flags{{"gameflow_DW", flow},
                    {"gameflow_DW_CHALLENGE_MIRROR_CHECKPOINT", kh02_checkpoint::mirror_scene_dw016},
                    {"gameflow_DW_GEAR_GETCOUNT", 40},
                    {"gameflow_DW_GEAR_COLLECTED_01", 1},
                    {"gameflow_DW_VISITED_2", 1},
                    {"GAMEFLOW_DW_MIRROR_STAIRS_IN", 1},
                    {"gameflow_DW_MIRROR_STAIRS_IN", 1},
                    {"gameflow_challenge_29_01", 1},
                    {"GAMEFLOW_CHALLENGE_29_02", 0}};
        const auto json = encoded(flags);
        assert(encoded(hub_entry_flags(flags)) == hub_entry_flags(json));
        assert(resume_castle_entry(flags) == resume_castle_entry(json));
        assert(white_gem_count(flags) == white_gem_count(json));
        for (int area = 4; area <= 6; ++area)
        {
            assert(encoded(mirror_room_entry_flags(flags, area)) == mirror_room_entry_flags(json, area));
        }
        for (const auto checkpoint : checkpoints)
        {
            assert(encoded(enter_flags(flags, checkpoint)) == enter_flags(json, checkpoint));
        }
        assert(enter_flags(json, kh02_checkpoint::gear_scene_dw011, true) == json);
        assert(enter_flags(flags, kh02_checkpoint::gear_scene_dw011, true) == flags);
    }
    // These APIs accept partial banks and must preserve unrelated JSON types.
    Json partial{{"untouched", Json::array({true, "value", nullptr})},
                 {"GAMEFLOW_DW_MIRROR_STAIRS_IN", "old"},
                 {"gameflow_DW_MIRROR_STAIRS_IN", true}};
    auto hub = hub_entry_flags(partial);
    assert(hub.at("untouched") == partial.at("untouched"));
    assert(hub.at("GAMEFLOW_DW_MIRROR_STAIRS_IN") == 0);
    assert(hub.at("gameflow_DW_MIRROR_STAIRS_IN") == 0);
    auto room = mirror_room_entry_flags(partial, 4);
    assert(room.at("GAMEFLOW_DW_MIRROR_STAIRS_IN") == 1 && room.at("gameflow_DW_MIRROR_STAIRS_IN") == 1);
    assert(room.at("untouched") == partial.at("untouched"));
    auto entered = enter_flags(partial, kh02_checkpoint::mirror_scene_dw015);
    assert(entered.at("untouched") == partial.at("untouched"));
    assert(entered.at("GAMEFLOW_DW_MIRROR_STAIRS_IN") == "old");
    assert(!entered.contains("gameflow_DW_CHALLENGE_MIRROR_CHECKPOINT"));
    assert(!resume_castle_entry(Json{}));
    for (const auto value : {Json(true), Json(2), Json(-1), Json(1.0), Json("1")})
    {
        bool rejected = false;
        try
        {
            (void)white_gem_count(Json{{"gameflow_challenge_29_01", value}});
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        assert(rejected);
    }
    for (const auto checkpoint : checkpoints)
    {
        if (checkpoint == kh02_checkpoint::gear_scene_dw011)
        {
            continue;
        }
        bool rejected = false;
        try
        {
            (void)enter_flags(partial, checkpoint, true);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        assert(rejected);
    }
}
