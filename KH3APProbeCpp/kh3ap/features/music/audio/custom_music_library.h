#pragma once

#include "kh3ap/features/music/shuffle/bgm_shuffle_table.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kh3ap
{
    // A player's custom track, from a WAV or OGG file in custom_music/.
    struct CustomMusicTrack
    {
        std::filesystem::path file{};
        // Seconds. The track plays from the top once, then loops between them;
        // no loop_end loops to the end of the file.
        std::optional<double> loop_start{};
        std::optional<double> loop_end{};
        // Gain on top of the game's music volume, to even out files.
        float volume{1.0f};
    };

    // Everything in custom_music/. Every WAV or OGG file replaces what its name
    // names. All files join Chaos when Include Custom Music is on.
    // custom_music.json optionally adds explicit targets, loop points and volume:
    //
    //   {"tracks": [{"file": "bbs_opening.wav", "replaces": ["GoA_01"],
    //                "loop_start": 12.5, "loop_end": 184.0, "volume": 0.8},
    //               {"replaces": ["BGM_he_battle"], "vanilla": true}]}
    //
    // An entry with "vanilla": true and no file keeps the game's own music for its
    // targets.
    //
    // A target is a level ("he_01": its field music; "GoA_01": the Garden's),
    // a world, by code or name: "he" or "Olympus" (all its music),
    // "he_battles" (its battle music), "he_exploration" (the rest: the field
    // music of all its levels); the most specific target wins. A track ("BGM_he_battle", or its full object path), or a keyblade form
    // ("Form_UltimaWeapon"). A target with a file or a vanilla entry is an
    // override: it applies where the game normally plays the target, over the
    // music shuffle; a file also holds form themes back, a vanilla entry
    // does not.
    struct CustomMusicLibrary
    {
        // By lowercased file name, including extension so WAV/OGG can coexist.
        std::unordered_map<std::wstring, CustomMusicTrack> tracks{};
        // Lowercased target -> track name.
        std::unordered_map<std::wstring, std::wstring> targets{};
        // Lowercased targets that keep the game's own music.
        std::unordered_set<std::wstring> vanilla_targets{};
        // Folder and manifest state; equal when nothing changed.
        std::wstring signature{};
        // Manifest entries that could not be used, for the log.
        std::vector<std::wstring> problems{};

        // The track replacing a level or a track, if any.
        [[nodiscard]] auto for_target(std::wstring_view target) const -> const CustomMusicTrack*;
        // A vanilla entry names the target.
        [[nodiscard]] auto keeps_vanilla(std::wstring_view target) const -> bool;
        // A file or a vanilla entry names the target.
        [[nodiscard]] auto overrides(std::wstring_view target) const -> bool
        {
            return for_target(target) != nullptr || keeps_vanilla(target);
        }
        // Most specific matching target: level, track, world scope, world.
        // A nonempty level denotes its field music. The returned target may
        // explicitly keep vanilla; callers must not fall through in that case.
        [[nodiscard]] auto override_target(std::wstring_view track, std::wstring_view level = {}) const -> std::wstring;
        // The track a table entry names (bgm_custom_track_prefix + name).
        [[nodiscard]] auto for_table_entry(std::wstring_view entry) const -> const CustomMusicTrack*;
        // The Chaos pool, as table entries, in a stable order.
        [[nodiscard]] auto pools() const -> BgmCustomPools;
    };

    // The client writes an absolute UTF-8 path beside the default folder. Empty
    // or absent configuration keeps the original mod-local custom_music folder.
    [[nodiscard]] auto configured_custom_music_directory(const std::filesystem::path& default_folder)
            -> std::filesystem::path;

    [[nodiscard]] auto load_custom_music_library(const std::filesystem::path& folder) -> CustomMusicLibrary;

    // Writes TARGETS.txt in the folder, creating the folder if needed: every
    // track a custom file can replace, and the levels, the Garden's plus
    // those the player has visited, so custom_music.json can be written
    // without guessing names.
    auto write_custom_music_targets(
            const std::filesystem::path& folder,
            const std::vector<std::wstring>& visited_levels) -> bool;

    // The visited levels a previous TARGETS.txt listed, so the list survives
    // restarts without a file of its own.
    [[nodiscard]] auto read_custom_music_visited_levels(const std::filesystem::path& folder)
            -> std::vector<std::wstring>;
}
