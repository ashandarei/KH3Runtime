#pragma once
#include "kh02_progress_state.h"
#include "kh02_story_checkpoints.h"
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace kh3ap::kh02_progress
{
    // Canonical typed checkpoint rules. JSON entry points adapt to these operations.
    inline auto hub_entry_flags(Flags flags) -> Flags
    {
        for (auto& [name, value] : flags)
        {
            auto key = name;
            for (auto& character : key)
            {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            if (key == "gameflow_dw_mirror_stairs_in" || key == "gameflow_dw_mirror_corridor_in" ||
                key == "gameflow_dw_mirror_c_tunnel_in")
            {
                value = 0;
            }
        }
        return flags;
    }

    inline auto mirror_room_entry_flags(Flags flags, int area) -> Flags
    {
        if (area < 4 || area > 6)
        {
            throw std::runtime_error("kh02_mirror_room_entry");
        }
        constexpr std::string_view names[]{"gameflow_DW_MIRROR_STAIRS_IN", "gameflow_DW_MIRROR_CORRIDOR_IN",
                                           "gameflow_DW_MIRROR_C_TUNNEL_IN"};
        for (int index = 0; index < 3; ++index)
        {
            auto normalized = std::string(names[index]);
            for (auto& character : normalized)
            {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            bool found = false;
            for (auto& [name, value] : flags)
            {
                auto key = name;
                for (auto& character : key)
                {
                    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                }
                if (key == normalized)
                {
                    value = index == area - 4 ? 1 : 0;
                    found = true;
                }
            }
            if (!found)
            {
                flags[std::string(names[index])] = index == area - 4 ? 1 : 0;
            }
        }
        return flags;
    }

    inline auto resume_castle_entry(const Flags& flags) -> bool
    {
        // A visit during DW009/DW010 has no live scene after re-entry.
        // Replay the unfinished opening so its source callbacks reach DW011.
        return flag_value(flags, "gameflow_DW_VISITED_2") == 1 &&
               flag_value(flags, "gameflow_DW") >= kh02_checkpoint::gear_scene_dw011;
    }

    struct EntryChange
    {
        Flags values;
        std::set<std::string> assigned;
    };

    inline auto enter_flags_change(Flags flags, int checkpoint, bool backtrack = false) -> EntryChange
    {
        std::set<std::string> assigned;
        if (backtrack)
        {
            if (checkpoint != kh02_checkpoint::gear_scene_dw011)
            {
                throw std::runtime_error("kh02_backtrack_checkpoint");
            }
            return {std::move(flags), std::move(assigned)};
        }
        const int flow = flag_value(flags, "gameflow_DW");
        constexpr const char* memory = "gameflow_DW_CHALLENGE_MIRROR_CHECKPOINT";
        if (checkpoint < kh02_checkpoint::mirror_scene_dw015)
        {
            if (flow >= kh02_checkpoint::mirror_scene_dw015)
            {
                flags[memory] = flow;
                assigned.insert(memory);
            }
            flags["gameflow_DW"] = checkpoint;
            assigned.insert("gameflow_DW");
            flags["gameflow_DW_GEAR_GETCOUNT"] = 0;
            assigned.insert("gameflow_DW_GEAR_GETCOUNT");
            for (auto entry = flags.begin(); entry != flags.end();)
            {
                if (entry->first.starts_with("gameflow_DW_GEAR_") && entry->first != "gameflow_DW_GEAR_GETCOUNT")
                {
                    entry = flags.erase(entry);
                }
                else
                {
                    ++entry;
                }
            }
        }
        else if (flow < kh02_checkpoint::mirror_scene_dw015)
        {
            flags["gameflow_DW"] = flag_value(flags, memory, checkpoint);
            assigned.insert("gameflow_DW");
            flags["gameflow_DW_GEAR_GETCOUNT"] = 0;
            assigned.insert("gameflow_DW_GEAR_GETCOUNT");
        }
        return {std::move(flags), std::move(assigned)};
    }

    inline auto enter_flags(Flags flags, int checkpoint, bool backtrack = false) -> Flags
    {
        return enter_flags_change(std::move(flags), checkpoint, backtrack).values;
    }

    inline auto white_gem_flag(std::string key) -> bool
    {
        for (auto& character : key)
        {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        constexpr std::string_view prefix = "gameflow_challenge_29_0";
        return key.starts_with(prefix) && key.size() == prefix.size() + 1 && key.back() >= '1' && key.back() <= '7';
    }

    inline auto white_gem_count(const Flags& flags) -> int
    {
        int count = 0;
        for (const auto& [name, value] : flags)
        {
            if (white_gem_flag(name))
            {
                if (value < 0 || value > 1)
                {
                    throw std::runtime_error("kh02_progress_range");
                }
                count += value;
            }
        }
        return count;
    }

} // namespace kh3ap::kh02_progress
