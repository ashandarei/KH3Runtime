#pragma once
#include "kh02_story_checkpoints.h"
#include "kh02_aqua_level_policy.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_ids.h"
#include <nlohmann/json.hpp>
#include "kh02_progress_state.h"
#include "kh3ap/features/kh02/wardrobe/kh02_cosmetic_policy.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap::kh02_progress
{
    using Json = nlohmann::json;
    inline constexpr std::size_t flag_name_limit = 96;
    inline constexpr std::size_t flag_count_limit = 256;
    inline constexpr int maximum_progress_value = 1'000'000;
    inline constexpr int maximum_color_channel = 255;

    // Normalize the owned validation copy once; it can then move directly into
    // the duplicate-name set instead of allocating two more string copies.
    inline auto normalize_flag_key(std::string& key) -> bool
    {
        if (key.empty() || key.size() > flag_name_limit)
        {
            return false;
        }
        for (auto& c : key)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            {
                return false;
            }
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        constexpr std::string_view tunnel = "gameflow_challenge_29_";
        return key.starts_with("gameflow_dw") || key == "gameflow_rebellis_clear" ||
               key == "gameflow_challenge_40_dw_05_memory" || key == "gameflow_challenge_45_dw_08_memory" ||
               key == "gameflow_challenge_49_dw_09_memory" || key == "gameflow_challenge_34_red_flower" ||
               key == "gameflow_challenge_34_blue_flower" || key == "gameflow_challenge_34_green_flower" ||
               key == "battlebonus_dw_dw_07_mirroraqua_03" || key == "battlebonus_dw_dw_07_mirroraqua_04" ||
               key == "battlebonus_dw_dw_02_deviltower" || key == "battlebonus_dw_dw_09_deviltower" ||
               key == "battlebonus_dw_dw_10_devilwave" || key == "battlebonus_dw_dw_08_darkside" ||
               key == "battlebonus_dw_dw_08_darkball" ||
               (key.starts_with(tunnel) && key.size() == tunnel.size() + 2 && key[tunnel.size()] == '0' &&
                key.back() >= '1' && key.back() <= '7');
    }

    inline auto flag_key(std::string key) -> bool
    {
        return normalize_flag_key(key);
    }

    inline auto integer(const Json& v, int minimum, int maximum) -> int
    {
        if (!v.is_number_integer() || v.is_number_unsigned() && v.get<uint64_t>() > static_cast<uint64_t>(maximum))
        {
            throw std::runtime_error("kh02_progress_integer");
        }
        const auto n = v.get<int64_t>();
        if (n < minimum || n > maximum)
        {
            throw std::runtime_error("kh02_progress_range");
        }
        return static_cast<int>(n);
    }

    inline auto from_json(const Json& value) -> SnapshotPtr;

    inline void validate(const Json& value)
    {
        (void)from_json(value);
    }

    inline void validate(const Snapshot& value)
    {
        if (value.flags.size() > flag_count_limit || !value.flags.contains("gameflow_DW"))
        {
            throw std::runtime_error("kh02_progress_flags");
        }
        std::set<std::string> normalized;
        for (const auto& [name, number] : value.flags)
        {
            auto key = name;
            if (!normalize_flag_key(key))
            {
                throw std::runtime_error("kh02_progress_flag_key");
            }
            const bool aqua_exp = key == kh02_level::normalized_exp_flag;
            if (!normalized.insert(std::move(key)).second)
            {
                throw std::runtime_error("kh02_progress_duplicate_flag");
            }
            if (number < (aqua_exp ? kh02_level::initial_exp : -1) ||
                number > (aqua_exp ? kh02_level::maximum_exp : maximum_progress_value))
            {
                throw std::runtime_error("kh02_progress_range");
            }
        }
        const int flow = value.flags.at("gameflow_DW");
        const bool castle = flow == kh02_checkpoint::gear_scene_dw009 || flow == kh02_checkpoint::gear_scene_dw010 ||
                            flow == kh02_checkpoint::gear_scene_dw011 || flow == kh02_checkpoint::gear_scene_dw012 ||
                            flow == kh02_checkpoint::first_gear || flow == kh02_checkpoint::second_gear ||
                            flow == kh02_checkpoint::third_gear || flow == kh02_checkpoint::fourth_gear ||
                            flow == kh02_checkpoint::castle_tower_start || flow == kh02_checkpoint::castle_tower_end ||
                            flow == kh02_checkpoint::gear_scene_dw013 || flow == kh02_checkpoint::gear_scene_dw014;
        const bool mirror = flow >= kh02_checkpoint::mirror_scene_dw015 &&
                            flow <= kh02_checkpoint::mirror_scene_dw020 &&
                            flow % kh02_checkpoint::mirror_checkpoint_step == 0;
        const bool forest =
            flow == kh02_checkpoint::forest_scene_dw021 || flow == kh02_checkpoint::forest_scene_dw022 ||
            flow == kh02_checkpoint::first_darkside_start || flow == kh02_checkpoint::first_darkside_end ||
            flow == kh02_checkpoint::forest_area_f_start || flow == kh02_checkpoint::forest_area_f_end ||
            flow == kh02_checkpoint::forest_scene_dw023 || flow == kh02_checkpoint::second_darkside_start ||
            flow == kh02_checkpoint::second_darkside_end || flow == kh02_checkpoint::forest_scene_dw024;
        const bool ending =
            flow == kh02_checkpoint::forest_scene_dw025_a || flow == kh02_checkpoint::forest_scene_dw025_b ||
            flow == kh02_checkpoint::depths_tower_start || flow == kh02_checkpoint::depths_tower_end ||
            flow == kh02_checkpoint::forest_scene_dw026 || flow == kh02_checkpoint::island_scene_dw027 ||
            flow == kh02_checkpoint::island_scene_dw028 || flow == kh02_checkpoint::demon_tide_start ||
            flow == kh02_checkpoint::demon_tide_end || flow == kh02_checkpoint::island_scene_dw029 ||
            flow == kh02_checkpoint::island_scene_dw030 || flow == kh02_checkpoint::island_scene_dw031 ||
            flow == kh02_checkpoint::island_scene_dw032 || flow == kh02_checkpoint::island_scene_dw033 ||
            flow == kh02_checkpoint::island_scene_dw034 || flow == kh02_checkpoint::island_scene_dw035 ||
            flow == kh02_checkpoint::magic_room_scene_yt036 || flow == kh02_checkpoint::cockpit_scene_gm037 ||
            flow == kh02_checkpoint::story_end;
        if (!castle && !mirror && !forest && !ending)
        {
            throw std::runtime_error("kh02_progress_checkpoint");
        }
        const auto validate_challenges = [](const auto& rows, int maximum)
        {
            if (rows.size() > kh02_challenge_id::count)
            {
                throw std::runtime_error("kh02_progress_challenges");
            }
            for (const auto& [index, number] : rows)
            {
                if (index < 0 || index >= kh02_challenge_id::count)
                {
                    throw std::runtime_error("kh02_progress_challenge_key");
                }
                const auto numeric = static_cast<int64_t>(number);
                if (numeric < 0 || numeric > maximum)
                {
                    throw std::runtime_error("kh02_progress_range");
                }
            }
        };
        validate_challenges(value.challenges, kh02_challenge_state::complete);
        validate_challenges(value.counts, maximum_progress_value);
        validate_challenges(value.events, 1);
        if (value.cosmetics)
        {
            const auto& selection = *value.cosmetics;
            if (selection.color_mode < WardrobeColorMode::Default || selection.color_mode > WardrobeColorMode::Gradient)
            {
                throw std::runtime_error("Invalid 0.2 wardrobe selection");
            }
            for (size_t part = 0; part < selection.equipped.size(); ++part)
            {
                if (selection.equipped[part] < 0 || selection.equipped[part] > kh02cosmetics::item_counts[part])
                {
                    throw std::runtime_error("Unknown 0.2 wardrobe item");
                }
            }
            for (const auto& color : {selection.top, selection.bottom})
            {
                for (const auto channel : color)
                {
                    if (channel < 0 || channel > maximum_color_channel)
                    {
                        throw std::runtime_error("Invalid 0.2 wardrobe color channel");
                    }
                }
            }
        }
        // The fixed row/key/value limits above bound the serialized record below
        // 32 KiB. Flags are ASCII identifiers, so JSON escaping cannot enlarge them.
    }

    inline auto freeze(Snapshot value) -> SnapshotPtr
    {
        validate(value);
        return std::make_shared<const Snapshot>(std::move(value));
    }

    inline auto cosmetic_selection(const Json& value) -> CosmeticSelection
    {
        kh02cosmetics::validate(value);
        return {value.at("equipped").get<std::array<int, 4>>(), value.at("color_mode").get<WardrobeColorMode>(),
                value.at("top").get<std::array<int, 3>>(), value.at("bottom").get<std::array<int, 3>>()};
    }

    inline auto to_json(const CosmeticSelection& value) -> Json
    {
        return {{"equipped", value.equipped},
                {"color_mode", static_cast<int>(value.color_mode)},
                {"top", value.top},
                {"bottom", value.bottom}};
    }

    // JSON is reserved for durable sidecars and the external inspection/command
    // boundary. Game-thread publication, worker jobs and native save captures share
    // immutable typed snapshots without encoding or parsing an internal envelope.
    inline auto from_json(const Json& value) -> SnapshotPtr
    {
        if (value.is_null())
        {
            return {};
        }
        if (!value.is_object() || (value.size() != 5 && (value.size() != 6 || !value.contains("cosmetics"))) ||
            value.value("schema", "") != "kh02.mirror_progress.v1" || value.dump().size() > 32768)
        {
            throw std::runtime_error("kh02_progress_schema");
        }
        Snapshot result;
        const auto& flags = value.at("flags");
        if (!flags.is_object() || flags.size() > flag_count_limit)
        {
            throw std::runtime_error("kh02_progress_flags");
        }
        for (auto entry = flags.begin(); entry != flags.end(); ++entry)
        {
            auto key = entry.key();
            normalize_flag_key(key);
            const bool aqua_exp = key == kh02_level::normalized_exp_flag;
            result.flags.emplace(entry.key(), integer(entry.value(), aqua_exp ? kh02_level::initial_exp : -1,
                                                      aqua_exp ? kh02_level::maximum_exp : maximum_progress_value));
        }
        const auto challenge_index = [](const std::string& key)
        {
            if (key.empty() || key.size() > 2 || (key.size() > 1 && key.front() == '0') ||
                !std::all_of(key.begin(), key.end(),
                             [](char character) { return character >= '0' && character <= '9'; }))
            {
                throw std::runtime_error("kh02_progress_challenge_key");
            }
            return std::stoi(key);
        };
        for (const auto* name : {"challenges", "counts", "events"})
        {
            const auto& rows = value.at(name);
            if (!rows.is_object() || rows.size() > kh02_challenge_id::count)
            {
                throw std::runtime_error("kh02_progress_challenges");
            }
            for (auto entry = rows.begin(); entry != rows.end(); ++entry)
            {
                const int index = challenge_index(entry.key());
                if (std::string_view(name) == "events")
                {
                    if (!entry.value().is_boolean())
                    {
                        throw std::runtime_error("kh02_progress_event");
                    }
                    result.events.emplace(index, entry.value().get<bool>());
                }
                else if (std::string_view(name) == "challenges")
                {
                    result.challenges.emplace(index, integer(entry.value(), 0, kh02_challenge_state::complete));
                }
                else
                {
                    result.counts.emplace(index, integer(entry.value(), 0, maximum_progress_value));
                }
            }
        }
        if (value.contains("cosmetics"))
        {
            result.cosmetics = cosmetic_selection(value.at("cosmetics"));
        }
        return freeze(std::move(result));
    }

    inline auto to_json(const SnapshotPtr& value) -> Json
    {
        if (!value)
        {
            return nullptr;
        }
        Json challenges = Json::object();
        Json counts = Json::object();
        Json events = Json::object();
        for (const auto& [index, state] : value->challenges)
        {
            challenges[std::to_string(index)] = state;
        }
        for (const auto& [index, count] : value->counts)
        {
            counts[std::to_string(index)] = count;
        }
        for (const auto& [index, active] : value->events)
        {
            events[std::to_string(index)] = active;
        }
        Json result{{"schema", "kh02.mirror_progress.v1"},
                    {"flags", value->flags},
                    {"challenges", std::move(challenges)},
                    {"counts", std::move(counts)},
                    {"events", std::move(events)}};
        if (value->cosmetics)
        {
            result["cosmetics"] = to_json(*value->cosmetics);
        }
        return result;
    }

} // namespace kh3ap::kh02_progress
