#pragma once
#include "kh3ap/bridge/catalog/chest_manifest.h"

namespace chest_fixture
{
    using Json = kh3ap::json::Value;
    inline auto data() -> Json
    { Json value; if (!kh3ap::json::parse(value, kh3ap::chest_manifest::shared_data)) throw std::runtime_error("fixture_data"); return value; }
    inline auto vanilla(size_t index = 0) -> Json
    {
        const auto shared = data();
        auto entry = shared["chests"].get_ref<const Json::object_t&>().begin();
        std::advance(entry, index);
        return Json{{"type", "treasure"}, {"source_id", entry->first},
            {"source_table", entry->second["table"]}, {"ap_location_id", entry->second["location_id"]},
            {"music_chest_kind", "vanilla"}, {"mom_song_id", 1}, {"mom_difficulty", 1},
            {"mom_song_name", shared["songs"]["1"]}};
    }
    inline auto placed(std::string_view id = "MOM_CHEST_001") -> Json
    {
        const auto shared = data(); const auto& placement = shared["placements"][id];
        return Json{{"type", "melody_of_memory"}, {"source_table", "MusicChest"},
            {"source_id", placement["treasure_id"]}, {"ap_location_id", placement["ap_location_id"]},
            {"music_chest_kind", "additional"}, {"mom_song_id", 1}, {"mom_difficulty", 2},
            {"mom_song_name", shared["songs"]["1"]}, {"music_chest_placement_version", 1},
            {"music_chest_placement_id", std::string(id)}, {"music_chest_map", placement["map_path"]},
            {"music_chest_position", placement["position"]}, {"music_chest_rotation", placement["rotation"]}};
    }
    inline auto seed(Json::array_t rows) -> Json
    { return Json{{"kh3_seed", Json{{"seed_name", "test"}, {"slot", 1}}}, {"portal_unlocks", 0}, {"bridge", Json{{"locations", std::move(rows)}}}}; }
    inline auto clears(Json::array_t ids) -> Json
    { return Json{{"schema", "kh3ap.mom_clear_state.v1"}, {"seed_name", "test"}, {"slot", 1}, {"cleared_locations", std::move(ids)}}; }
}
