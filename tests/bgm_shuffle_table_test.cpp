#include "kh3ap/features/music/shuffle/bgm_shuffle_table.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace
{
    using namespace kh3ap;

    auto world_of(const std::wstring& track) -> const BgmShuffleWorld&
    {
        for (const auto& world : bgm_shuffle_worlds())
        {
            for (const auto& list : {world.fields, world.battles})
            {
                for (const auto candidate : list)
                {
                    if (candidate == track)
                    {
                        return world;
                    }
                }
            }
        }
        assert(!"track belongs to no music world");
        std::abort();
    }

    auto contains(const std::vector<std::wstring_view>& list, const std::wstring& track) -> bool
    {
        for (const auto candidate : list)
        {
            if (candidate == track)
            {
                return true;
            }
        }
        return false;
    }

    auto group_tracks() -> std::vector<std::wstring_view>
    {
        std::vector<std::wstring_view> tracks{};
        for (const auto& group : bgm_shuffle_groups())
        {
            tracks.insert(tracks.end(), group.tracks.begin(), group.tracks.end());
        }
        return tracks;
    }
}

int main()
{
    // Every track is an object path owned by exactly one world.
    std::set<std::wstring_view> seen{};
    for (const auto& world : bgm_shuffle_worlds())
    {
        assert(!world.fields.empty());
        for (const auto& list : {world.fields, world.battles})
        {
            for (const auto track : list)
            {
                assert(seen.insert(track).second);
                const size_t dot = track.rfind(L'.');
                assert(track.starts_with(L"/Game/Sound/") && dot != std::wstring_view::npos);
                assert(track.substr(track.rfind(L'/', dot) + 1, dot - track.rfind(L'/', dot) - 1) ==
                       track.substr(dot + 1));
            }
        }
    }

    for (const auto& group : bgm_shuffle_groups())
    {
        assert(group.tracks.size() >= 2);
        for (const auto track : group.tracks)
        {
            assert(seen.insert(track).second);
            assert(track.starts_with(L"/Game/Sound/") && track.find(L"/BGM_") != std::wstring_view::npos);
            assert(bgm_shuffle_group_of(track) == &group);
        }
    }
    assert(!bgm_shuffle_group_of(bgm_shuffle_worlds().front().fields.front()));

    assert(build_bgm_swaps(BgmShuffleMode::Vanilla, L"seed").empty());
    assert(build_bgm_swaps(static_cast<BgmShuffleMode>(99), L"seed").empty());

    bool battles_paired_differently = false;
    bool seeds_differ = false;
    const auto reference = build_bgm_swaps(BgmShuffleMode::Worlds, L"0");
    for (const auto mode : {BgmShuffleMode::Worlds, BgmShuffleMode::BattleThemes})
    {
        for (int seed = 0; seed < 500; ++seed)
        {
            const std::wstring identity = std::to_wstring(seed);
            const auto swaps = build_bgm_swaps(mode, identity);
            assert(swaps == build_bgm_swaps(mode, identity));
            seeds_differ |= mode == BgmShuffleMode::Worlds && swaps != reference;

            // Each group: a derangement among its own tracks.
            for (const auto& group : bgm_shuffle_groups())
            {
                std::set<std::wstring> targets{};
                for (const auto track : group.tracks)
                {
                    const std::wstring& replacement = swaps.at(std::wstring{track});
                    assert(replacement != track && bgm_shuffle_group_of(replacement) == &group);
                    targets.insert(replacement);
                }
                assert(targets.size() == group.tracks.size());
            }

            // Field music: a derangement of the worlds, variants kept inside
            // one target world.
            std::set<std::wstring_view> field_targets{};
            for (const auto& world : bgm_shuffle_worlds())
            {
                const auto& target = world_of(swaps.at(std::wstring{world.fields[0]}));
                assert(target.code != world.code);
                for (const auto track : world.fields)
                {
                    const std::wstring& replacement = swaps.at(std::wstring{track});
                    assert(&world_of(replacement) == &target);
                    assert(contains(target.fields, replacement));
                }
                field_targets.insert(target.code);

                if (world.battles.empty())
                {
                    continue;
                }
                const auto& battle_target = world_of(swaps.at(std::wstring{world.battles[0]}));
                for (const auto track : world.battles)
                {
                    const std::wstring& replacement = swaps.at(std::wstring{track});
                    assert(&world_of(replacement) == &battle_target);
                    if (mode == BgmShuffleMode::Worlds)
                    {
                        // Field and battle move together.
                        assert(&battle_target == &target);
                        assert(contains(target.battles.empty() ? target.fields : target.battles, replacement));
                    }
                    else
                    {
                        // Battles come from another world with battle music.
                        assert(battle_target.code != world.code);
                        assert(contains(battle_target.battles, replacement));
                    }
                }
                battles_paired_differently |= &battle_target != &target;
            }
            assert(field_targets.size() == bgm_shuffle_worlds().size());
        }
    }
    assert(battles_paired_differently);
    assert(seeds_differ);

    // Chaos: one pool for every role, customs included; each original gets
    // a different track, used once, from another world. A group track takes
    // any track but itself, and no custom track. The other modes ignore the
    // custom pool.
    // custom:boss only in battle, custom:olympus only in Olympus.
    const BgmCustomPools custom{{
            {L"custom:boss", false, true, {}},
            {L"custom:olympus", true, true, {L"HE"}},
            {L"custom:theme"},
    }};
    assert(build_bgm_swaps(BgmShuffleMode::Worlds, L"0", custom) == reference);
    bool custom_used = false;
    bool roles_mixed = false;
    bool groups_mixed = false;
    bool group_kept_in_group = false;
    size_t originals = group_tracks().size();
    for (const auto& world : bgm_shuffle_worlds())
    {
        originals += world.fields.size() + world.battles.size();
    }
    for (int seed = 0; seed < 500; ++seed)
    {
        const std::wstring identity = std::to_wstring(seed);
        const auto swaps = build_bgm_swaps(BgmShuffleMode::Chaos, identity, custom);
        assert(swaps == build_bgm_swaps(BgmShuffleMode::Chaos, identity, custom));
        assert(swaps.size() == originals);
        std::set<std::wstring> used{};
        for (const auto& world : bgm_shuffle_worlds())
        {
            for (const auto& list : {world.fields, world.battles})
            {
                for (const auto track : list)
                {
                    const std::wstring& replacement = swaps.at(std::wstring{track});
                    assert(used.insert(replacement).second);
                    if (bgm_shuffle_group_of(replacement))
                    {
                        groups_mixed = true;
                        continue;
                    }
                    if (is_custom_bgm_track(replacement))
                    {
                        custom_used = true;
                        assert(replacement != L"custom:boss" || contains(world.battles, std::wstring{track}));
                        assert(replacement != L"custom:olympus" || world.code == L"HE");
                        continue;
                    }
                    const auto& target = world_of(replacement);
                    assert(target.code != world.code);
                    roles_mixed |= contains(world.fields, std::wstring{track}) != contains(target.fields, replacement);
                }
            }
        }
        for (const auto track : group_tracks())
        {
            const std::wstring& replacement = swaps.at(std::wstring{track});
            assert(used.insert(replacement).second);
            assert(!is_custom_bgm_track(replacement));
            assert(replacement != track);
            group_kept_in_group |= bgm_shuffle_group_of(replacement) == bgm_shuffle_group_of(track);
        }
    }
    assert(custom_used && roles_mixed && groups_mixed && group_kept_in_group);
    std::cout << "BGM shuffle table tests passed (1500 tables)\n";
}
