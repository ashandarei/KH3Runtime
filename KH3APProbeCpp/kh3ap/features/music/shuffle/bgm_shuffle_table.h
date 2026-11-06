#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kh3ap
{
    // The music_shuffle choice, in option and menu order.
    enum class BgmShuffleMode : int32_t
    {
        Vanilla = 0,
        // Each world's field and battle music move together to another world.
        Worlds = 1,
        // Field music and battle music are shuffled independently.
        BattleThemes = 2,
        // One pool of every field and battle track plus the player's custom
        // tracks: each original gets a different track, used once, from
        // another world, whatever its role.
        Chaos = 3,
    };

    // Custom tracks are not SEAD assets: the table names them with this
    // prefix and the file's name ("custom:bbs_opening"), and the runtime
    // plays them outside the game's audio.
    inline constexpr std::wstring_view bgm_custom_track_prefix = L"custom:";

    [[nodiscard]] inline auto is_custom_bgm_track(std::wstring_view track) -> bool
    {
        return track.starts_with(bgm_custom_track_prefix);
    }

    // A player's custom track in the Chaos pool, and where it may land.
    struct BgmCustomPoolTrack
    {
        // bgm_custom_track_prefix + the track's name.
        std::wstring entry{};
        bool field{true};
        bool battle{true};
        // World codes ("HE"); empty for any world.
        std::vector<std::wstring> worlds{};
    };

    struct BgmCustomPools
    {
        std::vector<BgmCustomPoolTrack> tracks{};
    };

    // A world's music as the game plays it: one field track per area
    // variant, and its battle tracks, as SEAD music object paths.
    struct BgmShuffleWorld
    {
        std::wstring_view code;
        std::wstring_view name;
        std::vector<std::wstring_view> fields;
        // Empty when the world's battles play its field track.
        std::vector<std::wstring_view> battles;
    };

    // Every world taking part in the shuffle.
    [[nodiscard]] auto bgm_shuffle_worlds() -> const std::vector<BgmShuffleWorld>&;

    // Music that belongs to no world, played on whichever slot the game
    // starts it on: attraction and Link summon themes, minigames, data
    // battles and Battlegates. Rhythm-locked minigames are left out.
    struct BgmShuffleGroup
    {
        std::wstring_view code;
        std::wstring_view name;
        std::vector<std::wstring_view> tracks;
    };

    [[nodiscard]] auto bgm_shuffle_groups() -> const std::vector<BgmShuffleGroup>&;

    // The group a track belongs to, or null.
    [[nodiscard]] auto bgm_shuffle_group_of(std::wstring_view track) -> const BgmShuffleGroup*;

    // Original -> replacement SEAD music object paths for field, battle and
    // group music, drawn from the seed identity so a seed always hears the
    // same shuffle. Empty for Vanilla or an unknown mode. Worlds without
    // battle music of their own play their field track in battle, so they
    // only take part in the field shuffle, and a world whose battle target
    // has no battle music takes that target's field track. A group's tracks
    // trade among themselves. Chaos maps the originals one to one into its
    // single pool, custom tracks included, where a group track may take any
    // track but itself and no custom track; the other modes ignore the
    // custom pool.
    [[nodiscard]] auto build_bgm_swaps(
            BgmShuffleMode mode,
            std::wstring_view seed_identity,
            const BgmCustomPools& custom = {}) -> std::unordered_map<std::wstring, std::wstring>;
}
