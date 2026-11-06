#pragma once
#include <utility>

#include "kh3ap/bridge/catalog/chest_data.h"
#include "kh3ap/bridge/checks/chest_snapshot.h"
#include "kh3ap/bridge/serialization/json.h"
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace kh3ap::chest_manifest
{
    inline constexpr auto maximum_music_difficulty = 2;

    inline constexpr size_t maximum_music_manifest_rows = 136;
    inline constexpr size_t maximum_keyblade_manifest_rows = 1024;

    using Json = kh3ap::json::Value;
    inline auto field(const Json& value, std::string_view key) -> const Json&
    {
        static const Json nil;
        return value.contains(key) ? value[key] : nil;
    }
    inline auto text(const Json& value) -> std::string
    {
        return value.is_string() ? value.get_ref<const std::string&>() : "";
    }
    inline auto integer(const Json& value) -> int64_t
    {
        if (!value.is_number() || !std::isfinite(value.get<double>()) ||
            std::floor(value.get<double>()) != value.get<double>() ||
            std::abs(value.get<double>()) > static_cast<double>(kh3ap::json::max_safe_integer))
        {
            throw std::runtime_error("chest_manifest_integer");
        }
        return static_cast<int64_t>(value.get<double>());
    }
    inline auto array(const Json& value) -> const Json::array_t&
    {
        if (!value.is_array())
        {
            throw std::runtime_error("chest_manifest_array");
        }
        return value.get_ref<const Json::array_t&>();
    }
    inline auto same_vector(const Json& value, const Json& expected) -> bool
    {
        const auto& actual = array(value);
        const auto& canonical = array(expected);
        if (actual.size() != 3 || canonical.size() != 3)
        {
            return false;
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            if (!actual[axis].is_number() || !canonical[axis].is_number() ||
                !std::isfinite(actual[axis].get<double>()) ||
                actual[axis].get<double>() != canonical[axis].get<double>())
            {
                return false;
            }
        }
        return true;
    }

    // Worker-only seed decoding. The client supplies external clear facts only.
    class Catalog
    {
        Json data;
        std::string seed;
        int64_t slot{};
        std::map<int64_t, Json> music_rows;
        std::map<std::string, Json> keyblade_rows;
        std::map<std::string, std::string> map_keyblades;
        std::set<std::string> eligible_keyblades;
        std::set<int64_t> challenges, cleared;

        auto envelope(std::string_view schema, int64_t process) const -> Json
        {
            return Json{{"schema", std::string(schema)},
                        {"owner", "dll"},
                        {"process_id", process},
                        {"seed_name", seed},
                        {"slot", slot},
                        {"chests", Json::array_t{}}};
        }
        auto validate_chest(const Json& row) const -> void
        {
            const auto& canonical = field(field(data, "chests"), text(field(row, "source_id")));
            if (text(field(canonical, "table")) != text(field(row, "source_table")) ||
                integer(field(canonical, "location_id")) != integer(field(row, "ap_location_id")))
            {
                throw std::runtime_error("chest_manifest_location_identity");
            }
        }
        auto music_row(const Json& row, bool placed) const -> Json
        {
            const auto song = integer(field(row, "mom_song_id"));
            const auto difficulty = integer(field(row, "mom_difficulty"));
            const auto name = text(field(field(data, "songs"), std::to_string(song)));
            if (name.empty() || name != text(field(row, "mom_song_name")) || difficulty < 0 ||
                difficulty > maximum_music_difficulty)
            {
                throw std::runtime_error("chest_manifest_song");
            }
            Json result{{"ap_location_id", integer(field(row, "ap_location_id"))},
                        {"treasure_id", text(field(row, "source_id"))},
                        {"song_id", song},
                        {"difficulty", difficulty},
                        {"song_name", name},
                        {"cleared", false}};
            if (!placed)
            {
                validate_chest(row);
                return result;
            }
            const auto placement = text(field(row, "music_chest_placement_id"));
            const auto& canonical = field(field(data, "placements"), placement);
            if (text(field(row, "source_table")) != "MusicChest" ||
                text(field(row, "source_id")) != text(field(canonical, "treasure_id")) ||
                integer(field(row, "ap_location_id")) !=
                    integer(field(canonical, "ap_location_id")) ||
                text(field(row, "music_chest_map")) != text(field(canonical, "map_path")) ||
                !same_vector(field(row, "music_chest_position"), field(canonical, "position")) ||
                !same_vector(field(row, "music_chest_rotation"), field(canonical, "rotation")))
            {
                throw std::runtime_error("chest_manifest_placement_identity");
            }
            const auto& position = array(field(canonical, "position"));
            const auto& rotation = array(field(canonical, "rotation"));
            if (position.size() != 3 || rotation.size() != 3)
            {
                throw std::runtime_error("chest_manifest_transform");
            }
            result["placement_version"] = 1;
            result["placement_id"] = placement;
            result["map"] = field(canonical, "map_path");
            result["x"] = position[0];
            result["y"] = position[1];
            result["z"] = position[2];
            result["yaw"] = rotation[1];
            return result;
        }

    public:
        Catalog()
        {
            if (!kh3ap::json::parse(data, shared_data))
            {
                throw std::runtime_error("chest_catalog_invalid");
            }
        }
        auto configure(const Json& metadata) -> void
        {
            const auto& identity = field(metadata, "kh3_seed");
            const auto next_seed = text(field(identity, "seed_name"));
            const auto next_slot = integer(field(identity, "slot"));
            if (next_seed.empty() || next_slot <= 0)
            {
                throw std::runtime_error("chest_manifest_seed_identity");
            }
            decltype(music_rows) next_music;
            decltype(keyblade_rows) next_keyblades;
            decltype(map_keyblades) next_map_keyblades;
            const auto& chestsanity = field(metadata, "chestsanity");
            const bool color_map = (chestsanity.is_boolean() && chestsanity.get<bool>()) ||
                                   (chestsanity.is_number() && chestsanity.get<double>() == 1);
            decltype(eligible_keyblades) next_eligible;
            if (color_map)
            {
                const auto doors = integer(field(metadata, "portal_unlocks"));
                if (doors != 0 && doors != 1)
                {
                    throw std::runtime_error("chest_manifest_portal_mode");
                }
                std::set<std::string> door_blades;
                for (const auto& name : array(field(data, "door_keyblades")))
                {
                    door_blades.insert(text(name));
                }
                for (const auto& [name, unused] :
                     field(data, "palettes").get_ref<const Json::object_t&>())
                {
                    if (name != "Pandora's Power" && name != "Ultima Weapon" &&
                        door_blades.contains(name) == (doors == 1))
                    {
                        next_eligible.insert(name);
                    }
                }
            }
            std::set<int64_t> next_challenges;
            std::set<std::string> music_ids;
            const auto& bridge = field(metadata, "bridge");
            if (bridge.contains("locations"))
            {
                for (const auto& row : array(bridge["locations"]))
                {
                    const auto type = text(field(row, "type"));
                    const bool converted =
                        type == "treasure" && text(field(row, "music_chest_kind")) == "vanilla";
                    if (converted || type == "melody_of_memory")
                    {
                        const auto location = integer(field(row, "ap_location_id"));
                        if (!next_challenges.insert(location).second)
                        {
                            throw std::runtime_error("chest_manifest_duplicate_challenge");
                        }
                        const bool placed =
                            field(row, "music_chest_placement_version").is_number() &&
                            integer(field(row, "music_chest_placement_version")) == 1;
                        if (converted || placed)
                        {
                            auto selected = music_row(row, placed);
                            if (!music_ids.insert(text(selected["treasure_id"])).second)
                            {
                                throw std::runtime_error("chest_manifest_duplicate_treasure");
                            }
                            next_music.emplace(location, std::move(selected));
                        }
                        continue; // Melody owns converted chests even if a stale Keyblade field
                                  // exists.
                    }
                    const auto blade = text(field(row, "keyblade_chest_keyblade"));
                    if (blade.empty())
                    {
                        continue;
                    }
                    const auto& palette = field(field(data, "palettes"), blade);
                    if (type != "treasure" || row.contains("mom_song_id") ||
                        text(field(palette, "token")).empty() ||
                        text(field(row, "keyblade_chest_token")) != text(field(palette, "token")) ||
                        !field(row, "keyblade_chest_native").is_boolean())
                    {
                        throw std::runtime_error("chest_manifest_keyblade_identity");
                    }
                    validate_chest(row);
                    if (color_map &&
                        !next_map_keyblades.emplace(text(field(row, "source_id")), blade).second)
                    {
                        throw std::runtime_error("chest_manifest_duplicate_map_keyblade");
                    }
                    if (row["keyblade_chest_native"].get<bool>())
                    {
                        continue;
                    }
                    const auto id = text(field(row, "source_id"));
                    Json selected{{"treasure_id", id},
                                  {"keyblade", field(palette, "token")},
                                  {"keyblade_name", blade}};
                    for (const auto part : {"body", "trim"})
                    {
                        const auto& colors = array(field(palette, part));
                        if (colors.size() != 3)
                        {
                            throw std::runtime_error("chest_manifest_palette");
                        }
                        for (size_t axis = 0; axis < 3; ++axis)
                        {
                            selected[std::string(part) + "_" + "rgb"[axis]] = colors[axis];
                        }
                    }
                    if (!next_keyblades.emplace(id, std::move(selected)).second)
                    {
                        throw std::runtime_error("chest_manifest_duplicate_keyblade");
                    }
                }
            }
            // Melody also wins if separate rows refer to the same treasure.
            for (const auto& [id, row] : next_music)
            {
                next_map_keyblades.erase(text(field(row, "treasure_id")));
            }
            if (next_music.size() > maximum_music_manifest_rows ||
                next_keyblades.size() > maximum_keyblade_manifest_rows ||
                next_map_keyblades.size() > maximum_keyblade_manifest_rows)
            {
                throw std::runtime_error("chest_manifest_limit");
            }
            seed = next_seed;
            slot = next_slot;
            music_rows = std::move(next_music);
            keyblade_rows = std::move(next_keyblades);
            challenges = std::move(next_challenges);
            cleared.clear();
            map_keyblades = std::move(next_map_keyblades);
            eligible_keyblades = std::move(next_eligible);
        }
        auto accept_clears(const Json& input) -> bool
        {
            if (text(field(input, "schema")) != "kh3ap.mom_clear_state.v1" ||
                text(field(input, "seed_name")) != seed || !field(input, "slot").is_number() ||
                integer(field(input, "slot")) != slot)
            {
                return false;
            }
            std::set<int64_t> next;
            for (const auto& value : array(field(input, "cleared_locations")))
            {
                const auto id = integer(value);
                if (!challenges.contains(id) || !next.insert(id).second)
                {
                    throw std::runtime_error("chest_manifest_clear_identity");
                }
            }
            // A delayed/reconnected snapshot cannot revoke a verified song clear.
            cleared.insert(next.begin(), next.end());
            return true;
        }
        auto music(const std::set<int64_t>& checked, int64_t process) const -> Json
        {
            auto result = envelope("kh3ap.music_chests.v1", process);
            auto& rows = result["chests"].get_ref<Json::array_t&>();
            // Preserve converted-before-placed order used by the original manifest.
            for (const bool placed : {false, true})
            {
                for (const auto& [id, row] : music_rows)
                {
                    if (row.contains("placement_version") != placed)
                    {
                        continue;
                    }
                    auto output = row;
                    output["cleared"] = cleared.contains(id) || (!placed && checked.contains(id));
                    rows.push_back(std::move(output));
                }
            }
            return result;
        }
        auto keyblades(int64_t process) const -> Json
        {
            auto result = envelope("kh3ap.keyblade_chests.v1", process);
            auto& rows = result["chests"].get_ref<Json::array_t&>();
            for (const auto& [id, row] : keyblade_rows)
            {
                rows.push_back(row);
            }
            return result;
        }
        auto snapshot(const std::set<int64_t>& checked, uint32_t process,
                      const std::filesystem::path& root, const auto& widen) const -> Snapshot
        {
            Snapshot result{
                .bridge_dir = root, .process_id = process, .seed = widen(seed), .slot = slot};
            for (const auto& [id, blade] : map_keyblades)
            {
                result.map_keyblades.emplace(
                    widen(id), MapKeyblade{widen(blade),
                                           widen(text(field(field(field(data, "palettes"), blade),
                                                            "token")))});
            }
            for (const auto& blade : eligible_keyblades)
            {
                result.eligible_keyblades.emplace(
                    widen(blade),
                    widen(text(field(field(field(data, "palettes"), blade), "token"))));
            }
            for (const auto& [id, row] : music_rows)
            {
                Assignment assignment{.location = id,
                                      .difficulty =
                                          static_cast<int>(integer(field(row, "difficulty"))),
                                      .song = widen(text(field(row, "song_name"))),
                                      .placed = row.contains("placement_version")};
                assignment.cleared =
                    cleared.contains(id) || (!assignment.placed && checked.contains(id));
                if (assignment.placed)
                {
                    assignment.map = widen(text(field(row, "map")));
                    assignment.placement = widen(text(field(row, "placement_id")));
                    const char* axes[] = {"x", "y", "z"};
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        assignment.position[axis] =
                            static_cast<float>(field(row, axes[axis]).get<double>());
                    }
                    assignment.yaw = static_cast<float>(field(row, "yaw").get<double>());
                }
                result.has_vanilla_assignments |= !assignment.placed;
                result.assignments.emplace(widen(text(field(row, "treasure_id"))),
                                           std::move(assignment));
            }
            for (const auto& [id, row] : keyblade_rows)
            {
                Assignment assignment;
                assignment.keyblade = widen(text(field(row, "keyblade")));
                assignment.keyblade_name = widen(text(field(row, "keyblade_name")));
                const char* axes[] = {"r", "g", "b"};
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    assignment.body[axis] = static_cast<float>(
                        field(row, std::string("body_") + axes[axis]).get<double>());
                    assignment.trim[axis] = static_cast<float>(
                        field(row, std::string("trim_") + axes[axis]).get<double>());
                }
                assignment.body[3] = assignment.trim[3] = 1.f;
                result.has_vanilla_assignments = true;
                // The same precedence as the JSON projection: Melody owns overlapping chests.
                result.assignments.try_emplace(widen(id), std::move(assignment));
            }
            return result;
        }
    };
} // namespace kh3ap::chest_manifest
