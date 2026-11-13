#include "kh3ap/core/settings/seed_ui_options.h"
#include "kh3ap/core/settings/seed_gameplay_options.h"
#include "kh3ap/bridge/serialization/json.h"
#include <cassert>
using namespace kh3ap;
int main() {
    for (int mode = 0; mode <= 3; ++mode) {
        for (bool custom : {false, true}) {
            for (bool forms : {false, true}) {
                const JsonObject slot{json::write(json::Value{
                    {"music_shuffle", mode}, {"include_custom_music", custom ? 1 : 0},
                    {"form_themes", forms}, {"unrelated", {{"music_shuffle", 99}}}})};
                const auto music = seed_music_options(slot);
                assert(music.shuffle_mode == mode && music.include_custom == custom && music.form_themes == forms);
            }
        }
    }
    const auto music_defaults = seed_music_options(JsonObject{"{}"});
    assert(music_defaults.shuffle_mode == 0 && music_defaults.include_custom && !music_defaults.form_themes);
    for (const auto value : {"-1", "4", "true", "null", "\"chaos\"", "2147483648"}) {
        const auto music = seed_music_options(JsonObject{
            std::string{"{\"music_shuffle\":"} + value + "}"});
        assert(music.shuffle_mode == 0);
    }
    for (const auto key : {"auto_skip_cutscenes", "dark_baymax_phase_1_skip", "max_level_ship",
            "fun_10k_heartless", "map_overlay_keybinds", "include_space_superbosses"}) {
        for (const auto scalar : {json::Value(0), json::Value(1), json::Value(false), json::Value(true)}) {
            const JsonObject options{json::write(json::Value{{key, scalar}, {"other", {{key, true}}}})};
            const bool enabled = scalar == 1 || scalar == true;
            assert(seed_bool_option(options, key) == enabled);
        }
        const JsonObject invalid{json::write(json::Value{{key, 2}, {"other", {{key, true}}}})};
        assert(!seed_bool_option(invalid, key));
        assert(seed_bool_option(invalid, key, true));
    }
    assert(seed_keyblade_levels_start_at_max(JsonObject{R"({"keyblade_levels":0})"}));
    assert(!seed_keyblade_levels_start_at_max(JsonObject{R"({"keyblade_levels":1,"other":{"keyblade_levels":0}})"}));
    assert(!seed_keyblade_levels_start_at_max(JsonObject{"{}"}));
    const ColorRandomizationMode color_modes[]{ColorRandomizationMode::Off,
                                               ColorRandomizationMode::Coordinated,
                                               ColorRandomizationMode::Wild};
    for (const int mode : {0, 1, 2}) {
        const json::Value metadata = {
            {"color_randomization", 99},
            {"slot_data", {{"color_randomization", mode},
                           {"unrelated", {{"color_randomization", 99}}}}}
        };
        assert(seed_color_randomization_default(JsonObject{json::write(metadata)}) == color_modes[mode]);
        assert(color_randomization_mode_id(color_modes[mode]) == mode);
    }
    for (const auto value : {"-1", "3", "true", "null", "\"wild\"", "4294967296", "-4294967296"}) {
        assert(seed_color_randomization_default(JsonObject{
            std::string{"{\"slot_data\":{\"color_randomization\":"} + value + "}}"}) == ColorRandomizationMode::Coordinated);
    }
    assert(seed_color_randomization_default(JsonObject{"{}"}) == ColorRandomizationMode::Coordinated);
    // Match the generated runtime document, including misleading keys outside
    // the authoritative slot/config objects. Exercise every supported EXP value.
    for (const int32_t half_steps : runtime_experience_multiplier_half_steps) {
        for (const bool sync : {false, true}) {
            const json::Value metadata = {
                {"exp_multiplier", 1.0}, {"battle_level_sync", !sync},
                {"slot_data", {
                    {"battle_level_mode", sync ? 1 : 0},
                    {"kh3_randomizer", {
                        {"exp_multiplier", half_steps / 2.0},
                        {"battle_level_sync", sync}
                    }}
                }}
            };
            const JsonObject document{json::write(metadata)};
            assert(seed_experience_multiplier_default(document) == half_steps);
            assert(seed_battle_level_sync_default(document) == sync);
        }
    }
    assert(seed_experience_multiplier_default(JsonObject{"{}"}) == 2);
    assert(seed_experience_multiplier_default(JsonObject{R"({"slot_data":{"kh3_randomizer":{"exp_multiplier":1.5}}})"}) == 2);
    assert(!seed_battle_level_sync_default(JsonObject{R"({"slot_data":{"battle_level_mode":0}})"}));
    const auto defaults = read_runtime_ui_seed_defaults(JsonObject{"{}"});
    assert(defaults.map_icon_options == 63 && defaults.map_overlay_enabled);
    assert(defaults.song_display);
    assert(defaults.auto_equip_essential_abilities);
    const auto essential_off = read_runtime_ui_seed_defaults(
        JsonObject{R"({"auto_equip_essential_abilities":0})"});
    assert(!essential_off.auto_equip_essential_abilities);
    assert(!apply_saved_map_ui_options(JsonObject{"{}"}, essential_off).auto_equip_essential_abilities);
    assert(apply_saved_map_ui_options(
        JsonObject{R"({"auto_equip_essential_abilities":true})"}, essential_off).auto_equip_essential_abilities);
    assert(!apply_saved_map_ui_options(
        JsonObject{R"({"auto_equip_essential_abilities":false})"}, defaults).auto_equip_essential_abilities);
    const auto seed = read_runtime_ui_seed_defaults(JsonObject{R"({"map_overlay_enabled":0,"song_display":0,
        "map_chest_icons":0,"map_emblem_icons":0,"map_ingredient_icons":0,"minimap_icons":0,"large_map_icons":0,
        "hide_locked_chest_icons":1,"battle_level_offset":10,"melody_of_memory_party":3,
        "melody_of_memory_play_style":2,"melody_of_memory_starting_item":3})"});
    assert(seed.map_icon_options == 0 && !seed.map_overlay_enabled);
    assert(!seed.song_display);
    assert(seed.battle_level_offset == 10 && seed.mom[0] == 2 && seed.mom[1] == 3 && seed.mom[2] == 3);
    const auto saved = apply_saved_map_ui_options(JsonObject{R"({"map_overlay_enabled":true,"song_display":true,
        "map_chest_icons":true,"map_locked_chest_icons":true})"}, seed);
    assert(saved.map_overlay_enabled && saved.map_icon_options == 33);
    assert(saved.song_display);
    assert(!apply_saved_map_ui_options(JsonObject{"{}"}, seed).song_display);
    assert(!apply_saved_map_ui_options(JsonObject{R"({"song_display":false})"}, defaults).song_display);
    assert(apply_saved_map_ui_options(JsonObject{"{}"}, seed).map_icon_options == 0);
    const auto invalid = read_runtime_ui_seed_defaults(JsonObject{R"({"battle_level_offset":6,
        "melody_of_memory_party":4,"melody_of_memory_play_style":-1,"melody_of_memory_starting_item":true})"});
    assert(invalid.battle_level_offset == 0 && invalid.mom == defaults.mom);
}
