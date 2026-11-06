#pragma once
#include "kh02_story_checkpoints.h"
#include "kh02_progress_policy.h"

namespace kh3ap::kh02_gameflow
{
    using Json = nlohmann::json;

    inline auto normalized(std::string name) -> std::string
    {
        for (auto& character : name)
        {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return name;
    }

    inline auto flag_name(const Json& flags, const std::string& requested) -> std::string
    {
        if (!kh02_progress::flag_key(requested))
        {
            throw std::runtime_error("Unknown 0.2 story flag namespace");
        }
        const auto key = normalized(requested);
        // The main checkpoint has a canonical spelling in the save schema.
        if (key == "gameflow_dw")
        {
            return "gameflow_DW";
        }
        for (auto entry = flags.begin(); entry != flags.end(); ++entry)
        {
            if (normalized(entry.key()) == key)
            {
                return entry.key();
            }
        }
        return requested;
    }

    inline auto read(const Json& progress, const std::string& requested) -> int
    {
        const Json flags = progress.is_null() ? Json::object() : progress.at("flags");
        const auto key = flag_name(flags, requested);
        return flags.value(key, key == "gameflow_DW" ? kh02_checkpoint::gear_scene_dw011 : 0);
    }

    inline void complete_previous_story(Json& flags, int checkpoint)
    {
        using namespace kh02_checkpoint;

        struct Completion
        {
            int checkpoint;
            const char* flag;
            int value{1};
        };

        // These are completed story milestones, not battle-start checkpoints.
        // The mirror routes and gears can be taken in any order: only mark all
        // their individual routes complete once the story requires all of them.
        // Optional mirror battles, treasures and challenge progress stay earned.
        constexpr Completion completions[]{
            {first_gear, "gameflow_DW_GEAR_GETCOUNT", kh02_gear_counter::one_collected},
            {second_gear, "gameflow_DW_GEAR_GETCOUNT", kh02_gear_counter::two_collected},
            {third_gear, "gameflow_DW_GEAR_GETCOUNT", kh02_gear_counter::three_collected},
            {fourth_gear, "gameflow_DW_GEAR_GETCOUNT", kh02_gear_counter::all_collected},
            {castle_tower_end, "BattleBonus_DW_dw_02_DevilTower"},
            {mirror_scene_dw015, "gameflow_DW_VISITED_2"},
            {mirror_scene_dw015, "gameflow_DW_TUTORIAL_10_END"},
            {mirror_scene_dw015, "gameflow_DW_TUTORIAL_11_END"},
            {mirror_scene_dw015, "gameflow_DW_TUTORIAL_12_END"},
            {mirror_scene_dw015, "gameflow_DW_TUTORIAL_13_END"},
            {mirror_scene_dw015, "gameflow_DW_TUTORIAL_14_END"},
            {mirror_scene_dw015, "gameflow_DW_TUTORIAL_15_END"},
            {mirror_scene_dw019, "gameflow_DW_VISITED_3"},
            {mirror_scene_dw019, "gameflow_DW_VISITED_4"},
            {mirror_scene_dw019, "gameflow_DW_VISITED_5"},
            {mirror_scene_dw019, "gameflow_DW_VISITED_6"},
            {mirror_scene_dw019, "gameflow_DW_VISITED_7"},
            {mirror_scene_dw019, "gameflow_DW_MIRROR_STAIRS_CLEAR"},
            {mirror_scene_dw019, "gameflow_DW_MIRROR_CORRIDOR_CLEAR"},
            {mirror_scene_dw019, "gameflow_DW_MIRROR_C_TUNNEL_CLEAR"},
            {mirror_scene_dw019, "gameflow_DW_06_MIRROREventA_END"},
            {first_darkside_end, "BattleBonus_DW_dw_08_Darkside"},
            {forest_area_f_end, "BattleBonus_DW_dw_08_Darkball"},
            {second_darkside_end, "gameflow_DW_AP_DARKSIDE_02_CLEAR"},
            {forest_scene_dw024, "gameflow_DW_VISITED_8"},
            {forest_scene_dw024, "gameflow_DW_08_DARKPUNCH_A_FINISH"},
            {forest_scene_dw024, "gameflow_DW_08_DARKPUNCH_B_FINISH"},
            {forest_scene_dw024, "gameflow_DW_08_PHANTOM_01"},
            {forest_scene_dw024, "gameflow_DW_08_PHANTOM_02"},
            {forest_scene_dw024, "gameflow_DW_08_PHANTOM_04"},
            {forest_scene_dw024, "gameflow_DW_08_BATTLE_CAMERA_01"},
            {depths_tower_end, "BattleBonus_DW_dw_09_DevilTower"},
            {island_scene_dw027, "gameflow_DW_VISITED_9"},
            // The final victory flag also drives the fixed local Aqua reward
            // through the normal check journal and AP receipt delivery.
            {demon_tide_end, "BattleBonus_DW_dw_10_DevilWave"},
            {island_scene_dw029, "gameflow_DW_VISITED_10"},
            {story_end, "gameflow_REBELLIS_CLEAR"},
        };
        for (const auto& completion : completions)
        {
            if (checkpoint >= completion.checkpoint)
            {
                const auto name = flag_name(flags, completion.flag);
                flags[name] = completion.value == 1 ? 1 : std::max(flags.value(name, 0), completion.value);
            }
        }
    }

    inline auto update(Json progress, const std::string& requested, int value) -> Json
    {
        if (progress.is_null())
        {
            progress = {{"schema", "kh02.mirror_progress.v1"},
                        {"flags", {{"gameflow_DW", kh02_checkpoint::gear_scene_dw011}}},
                        {"challenges", Json::object()},
                        {"counts", Json::object()},
                        {"events", Json::object()}};
        }
        auto& flags = progress.at("flags");
        const auto name = flag_name(flags, requested);
        flags[name] = value;
        kh02_progress::validate(progress);
        if (name == "gameflow_DW")
        {
            complete_previous_story(flags, value);
        }
        kh02_progress::validate(progress);
        return progress;
    }
} // namespace kh3ap::kh02_gameflow
