#include "kh3ap/features/music/shuffle/bgm_shuffle_table.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto minimum_shuffle_candidates = 2;

        constexpr auto shuffle_hash_offset_basis = 14695981039346656037ULL;
        constexpr auto shuffle_hash_prime = 1099511628211ULL;
        constexpr auto splitmix_state_increment = 0x9E3779B97F4A7C15ULL;
        constexpr auto splitmix_first_multiplier = 0xBF58476D1CE4E5B9ULL;
        constexpr auto splitmix_second_multiplier = 0x94D049BB133111EBULL;
        constexpr auto splitmix_first_shift = 30;
        constexpr auto splitmix_second_shift = 27;
        constexpr auto splitmix_final_shift = 31;
        constexpr auto max_shuffle_repair_passes = 8;
        constexpr auto music_assignment_attempt_limit = 1000;
        constexpr auto music_group_shuffle_attempt_limit = 100;

        // Native world catalogue. Pooling and override rules are documented in
        // docs/MUSIC_DEVELOPMENT.md.
        auto music_worlds() -> const std::vector<BgmShuffleWorld>&
        {
            static const std::vector<BgmShuffleWorld> worlds{
                {L"HE",
                 L"Olympus",
                 {L"/Game/Sound/BGM/BGM_he_field.BGM_he_field",
                  L"/Game/Sound/BGM/BGM_he_field_2.BGM_he_field_2"},
                 {L"/Game/Sound/BGM/BGM_he_battle.BGM_he_battle",
                  L"/Game/Sound/BGM/BGM_he_battle_2.BGM_he_battle_2"}},
                {L"TT",
                 L"Twilight Town",
                 {L"/Game/Sound/BGM/BGM_theafter_field.BGM_theafter_field"},
                 {L"/Game/Sound/BGM/BGM_workingt.BGM_workingt"}},
                {L"TS",
                 L"Toy Box",
                 {L"/Game/Sound/BGM/BGM_ts_field.BGM_ts_field"},
                 {L"/Game/Sound/BGM/BGM_ts_battle.BGM_ts_battle"}},
                {L"RA",
                 L"Kingdom of Corona",
                 {L"/Game/Sound/BGM/BGM_ra_field.BGM_ra_field",
                  L"/Game/Sound/BGM/BGM_ra_field_2.BGM_ra_field_2"},
                 {L"/Game/Sound/BGM/BGM_ra_battle.BGM_ra_battle",
                  L"/Game/Sound/BGM/BGM_ra_battle_2.BGM_ra_battle_2"}},
                {L"MI",
                 L"Monstropolis",
                 {L"/Game/Sound/BGM/BGM_mi_field.BGM_mi_field",
                  L"/Game/Sound/BGM/BGM_mi_field_2.BGM_mi_field_2"},
                 {L"/Game/Sound/BGM/BGM_mi_battle.BGM_mi_battle"}},
                {L"FZ",
                 L"Arendelle",
                 {L"/Game/Sound/BGM/BGM_fz_field.BGM_fz_field",
                  L"/Game/Sound/BGM/BGM_fz_field_2.BGM_fz_field_2"},
                 {L"/Game/Sound/BGM/BGM_fz_battle.BGM_fz_battle",
                  L"/Game/Sound/BGM/BGM_fz_battle_2.BGM_fz_battle_2"}},
                {L"CA",
                 L"The Caribbean",
                 {L"/Game/Sound/BGM/BGM_ca_field.BGM_ca_field",
                  L"/Game/Sound/BGM/BGM_ca_field_2.BGM_ca_field_2",
                  L"/Game/Sound/BGM/ca/BGM_ca_field_3.BGM_ca_field_3"},
                 {L"/Game/Sound/BGM/BGM_ca_battle.BGM_ca_battle",
                  L"/Game/Sound/BGM/BGM_ca_battle_2.BGM_ca_battle_2"}},
                {L"BX",
                 L"San Fransokyo",
                 {L"/Game/Sound/BGM/BGM_bx_field.BGM_bx_field",
                  L"/Game/Sound/BGM/BGM_bx_field_2.BGM_bx_field_2"},
                 {L"/Game/Sound/BGM/BGM_bx_battle.BGM_bx_battle"}},
                {L"PO", L"100 Acre Wood", {L"/Game/Sound/BGM/BGM_poo_field.BGM_poo_field"}, {}},
                {L"KG", L"Keyblade Graveyard", {L"/Game/Sound/BGM/BGM_kg_field.BGM_kg_field"}, {}},
                {L"EW",
                 L"The Final World",
                 {L"/Game/Sound/BGM/ew/BGM_ew751_cut.BGM_ew751_cut",
                  L"/Game/Sound/BGM/BGM_ew_field.BGM_ew_field"},
                 {L"/Game/Sound/BGM/BGM_ew_battle.BGM_ew_battle"}},
                {L"GOA",
                 L"Garden of Assemblage",
                 {L"/Game/Sound/_DLC/BGM/BGM_field_rg03.BGM_field_rg03"},
                 {}},
            };
            return worlds;
        }

        // Seen in game (2026-09-28): attractions on
        // the Attraction slot, the Classic Kingdom menu on Menu, Flans and
        // Flash Tracer on MiniGame, Battlegates on Mission, data battles on
        // Boss.
        auto music_groups() -> const std::vector<BgmShuffleGroup>&
        {
            static const std::vector<BgmShuffleGroup> groups{
                {L"LINK",
                 L"Attractions and Links",
                 {L"/Game/Sound/BGM/BGM_attraction.BGM_attraction",
                  L"/Game/Sound/BGM/BGM_link_ariel.BGM_link_ariel",
                  L"/Game/Sound/BGM/BGM_link_dream.BGM_link_dream",
                  L"/Game/Sound/BGM/BGM_link_ralph.BGM_link_ralph",
                  L"/Game/Sound/BGM/BGM_link_simba.BGM_link_simba",
                  L"/Game/Sound/BGM/BGM_link_stitch.BGM_link_stitch"}},
                // Flantastic Flans, Flash Tracer, mountcurling, the
                // Classic Kingdom menu, cooking and Winnie the Pooh. The
                // festival dance follows its music, so it stays out, as
                // do the cooking result jingles.
                {L"MINIGAME",
                 L"Minigames",
                 {L"/Game/Sound/BGM/BGM_pudding_battle.BGM_pudding_battle",
                  L"/Game/Sound/BGM/BGM_pudding_camera.BGM_pudding_camera",
                  L"/Game/Sound/BGM/BGM_pudding_he.BGM_pudding_he",
                  L"/Game/Sound/BGM/BGM_flashtracer.BGM_flashtracer",
                  L"/Game/Sound/BGM/BGM_mountcurling.BGM_mountcurling",
                  L"/Game/Sound/BGM/BGM_lsi_menu.BGM_lsi_menu",
                  L"/Game/Sound/BGM/BGM_cook_first.BGM_cook_first",
                  L"/Game/Sound/BGM/BGM_cook_jojo_first.BGM_cook_jojo_first",
                  L"/Game/Sound/BGM/BGM_cook_menu.BGM_cook_menu",
                  L"/Game/Sound/BGM/BGM_poo_mini.BGM_poo_mini"}},
                {L"REMIND",
                 L"Data battles and Battlegates",
                 {L"/Game/Sound/_DLC/BGM/BGM_battle_rg10.BGM_battle_rg10",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg11.BGM_battle_rg11",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg12.BGM_battle_rg12",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg13.BGM_battle_rg13",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg14.BGM_battle_rg14",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg15.BGM_battle_rg15",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg16.BGM_battle_rg16",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg17.BGM_battle_rg17",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg18.BGM_battle_rg18",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg19.BGM_battle_rg19",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg20.BGM_battle_rg20",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg21.BGM_battle_rg21",
                  L"/Game/Sound/_DLC/BGM/BGM_battle_rg22.BGM_battle_rg22",
                  L"/Game/Sound/BGM/BGM_portal_battle_1.BGM_portal_battle_1",
                  L"/Game/Sound/BGM/BGM_portal_battle_2.BGM_portal_battle_2",
                  L"/Game/Sound/BGM/BGM_portal_battle_3.BGM_portal_battle_3"}},
                // Story fights the level starts on the Mission or Boss
                // slot. Rock Titan, the Shrouding and the Organization
                // duels are in BOSS.
                {L"SCRIPTED",
                 L"Scripted fights",
                 {L"/Game/Sound/BGM/bt/BGM_bt902_battle.BGM_bt902_battle",
                  L"/Game/Sound/BGM/bx/BGM_bx557_battle.BGM_bx557_battle",
                  L"/Game/Sound/BGM/bx/BGM_bx559_battle.BGM_bx559_battle",
                  L"/Game/Sound/BGM/bx/BGM_bx562_battle.BGM_bx562_battle",
                  L"/Game/Sound/BGM/ca/BGM_ca406_battle.BGM_ca406_battle",
                  L"/Game/Sound/BGM/ca/BGM_ca412_battle.BGM_ca412_battle",
                  L"/Game/Sound/BGM/ca/BGM_ca416_battle.BGM_ca416_battle",
                  L"/Game/Sound/BGM/dp/BGM_dp653_battle.BGM_dp653_battle",
                  L"/Game/Sound/BGM/dw/BGM_dw152_battle.BGM_dw152_battle",
                  L"/Game/Sound/BGM/dw_2/BGM_dw601_battle.BGM_dw601_battle",
                  L"/Game/Sound/BGM/dw_2/BGM_dw602_battle.BGM_dw602_battle",
                  L"/Game/Sound/BGM/fz/BGM_fz457_mission.BGM_fz457_mission",
                  L"/Game/Sound/BGM/fz/BGM_fz465a_battle.BGM_fz465a_battle",
                  L"/Game/Sound/BGM/BGM_han_mission_1.BGM_han_mission_1",
                  L"/Game/Sound/BGM/BGM_han_mission_2.BGM_han_mission_2",
                  L"/Game/Sound/BGM/BGM_han_mission_3.BGM_han_mission_3",
                  L"/Game/Sound/BGM/he/BGM_he110_battle.BGM_he110_battle",
                  L"/Game/Sound/BGM/he/BGM_he115_battle.BGM_he115_battle",
                  L"/Game/Sound/BGM/he/BGM_he116_battle_1.BGM_he116_battle_1",
                  L"/Game/Sound/BGM/he/BGM_he116_battle_2.BGM_he116_battle_2",
                  L"/Game/Sound/BGM/kg/BGM_kg701_battle.BGM_kg701_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg852a_battle.BGM_kg852a_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg852b_battle.BGM_kg852b_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg856_battle.BGM_kg856_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg857_battle.BGM_kg857_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg861_battle.BGM_kg861_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg867_battle.BGM_kg867_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg867_battle_2.BGM_kg867_battle_2",
                  L"/Game/Sound/BGM/kg_3/BGM_kg871_battle.BGM_kg871_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg872_Battle.BGM_kg872_Battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg874_battle.BGM_kg874_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg874_battle_re.BGM_kg874_battle_re",
                  L"/Game/Sound/BGM/mi/BGM_mi352_battle.BGM_mi352_battle",
                  L"/Game/Sound/BGM/mi/BGM_mi371_battle.BGM_mi371_battle",
                  L"/Game/Sound/BGM/ra/BGM_ra310_battle.BGM_ra310_battle",
                  L"/Game/Sound/BGM/ra/BGM_ra316_2_battle.BGM_ra316_2_battle",
                  L"/Game/Sound/BGM/ra/BGM_ra320_battle.BGM_ra320_battle",
                  L"/Game/Sound/BGM/ra/BGM_ra321_battle.BGM_ra321_battle",
                  L"/Game/Sound/BGM/ra/BGM_ra323_battle.BGM_ra323_battle",
                  L"/Game/Sound/BGM/ts/BGM_ts261_battle.BGM_ts261_battle",
                  L"/Game/Sound/BGM/ts/BGM_ts267_battle.BGM_ts267_battle",
                  L"/Game/Sound/BGM/tt/BGM_tt202_battle.BGM_tt202_battle",
                  L"/Game/Sound/BGM/tt/BGM_tt208_battle.BGM_tt208_battle"}},
                // Seen in game (2026-09-29): the world map (wm_01) on
                // Field, the editor on Menu, a Gummi route (gm_01) on
                // Field and Battle. The departure and result jingles
                // (gumi_start on Event, gumi_result on Menu) stay out, as
                // do gumi_therm, gumi_therm_or and gumi_afterima, never
                // heard. Last, so the other groups' draws do not change.
                {L"GUMMI",
                 L"Gummi Ship and world map",
                 {L"/Game/Sound/BGM/BGM_world_field.BGM_world_field",
                  L"/Game/Sound/BGM/BGM_gumi_edit.BGM_gumi_edit",
                  L"/Game/Sound/BGM/BGM_gumi_field_1.BGM_gumi_field_1",
                  L"/Game/Sound/BGM/BGM_gumi_field_2.BGM_gumi_field_2",
                  L"/Game/Sound/BGM/BGM_gumi_field_3.BGM_gumi_field_3",
                  L"/Game/Sound/BGM/BGM_gumi_battle.BGM_gumi_battle",
                  L"/Game/Sound/BGM/BGM_gumi_battle_2.BGM_gumi_battle_2"}},
                // Boss themes (user request, 2026-09-29), on the Boss
                // slot. The final fight's Attraction Flow segment plays
                // xeha_bat__boss_3 on Event, which the shuffle never
                // touches, so it keeps its music. Last, after GUMMI.
                {L"BOSS",
                 L"Bosses",
                 {L"/Game/Sound/BGM/BGM_chu_bat_boss_1.BGM_chu_bat_boss_1",
                  L"/Game/Sound/BGM/BGM_dis_bat_boss_2.BGM_dis_bat_boss_2",
                  L"/Game/Sound/BGM/BGM_han_bat_boss_1.BGM_han_bat_boss_1",
                  L"/Game/Sound/BGM/ca/BGM_han_bat_boss_ca.BGM_han_bat_boss_ca",
                  L"/Game/Sound/BGM/ca/BGM_han_bat_boss_ca_2.BGM_han_bat_boss_ca_2",
                  L"/Game/Sound/BGM/BGM_hless_bat_boss_1.BGM_hless_bat_boss_1",
                  L"/Game/Sound/BGM/BGM_hless_bat_boss_2.BGM_hless_bat_boss_2",
                  L"/Game/Sound/BGM/kg_3/BGM_kg_xiii_a1_battle.BGM_kg_xiii_a1_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg_xiii_a2_battle.BGM_kg_xiii_a2_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg_xiii_b1_battle.BGM_kg_xiii_b1_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg_xiii_b2_battle.BGM_kg_xiii_b2_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg_xiii_b3_battle.BGM_kg_xiii_b3_battle",
                  L"/Game/Sound/BGM/kg_3/BGM_kg_xiii_c_battle.BGM_kg_xiii_c_battle",
                  L"/Game/Sound/BGM/BGM_rocktitan_battle.BGM_rocktitan_battle",
                  L"/Game/Sound/BGM/BGM_shrouding_battle.BGM_shrouding_battle",
                  L"/Game/Sound/BGM/BGM_xeha_bat__boss_1.BGM_xeha_bat__boss_1",
                  L"/Game/Sound/BGM/BGM_xeha_bat__boss_2.BGM_xeha_bat__boss_2",
                  L"/Game/Sound/BGM/BGM_xeha_bat__boss_3.BGM_xeha_bat__boss_3",
                  L"/Game/Sound/BGM/BGM_XIII_bat_boss_1.BGM_XIII_bat_boss_1",
                  L"/Game/Sound/BGM/BGM_XIII_bat_boss_2.BGM_XIII_bat_boss_2",
                  L"/Game/Sound/BGM/BGM_XIII_bat_boss_3.BGM_XIII_bat_boss_3"}},
            };
            return groups;
        }

        // splitmix64 seeded from an FNV-1a hash of the identity: stable
        // across compilers and runs, unlike the standard distributions.
        class ShuffleRng
        {
        public:
            explicit ShuffleRng(std::wstring_view identity)
            {
                state_ = shuffle_hash_offset_basis;
                for (const wchar_t ch : identity)
                {
                    state_ ^= static_cast<uint64_t>(ch);
                    state_ *= shuffle_hash_prime;
                }
                // Keeps the draw independent from other seed-derived features.
                for (const wchar_t ch : std::wstring_view{L"|music_shuffle"})
                {
                    state_ ^= static_cast<uint64_t>(ch);
                    state_ *= shuffle_hash_prime;
                }
            }

            auto next() -> uint64_t
            {
                uint64_t z = (state_ += splitmix_state_increment);
                z = (z ^ (z >> splitmix_first_shift)) * splitmix_first_multiplier;
                z = (z ^ (z >> splitmix_second_shift)) * splitmix_second_multiplier;
                return z ^ (z >> splitmix_final_shift);
            }

            auto below(size_t bound) -> size_t
            {
                return static_cast<size_t>(next() % bound);
            }

        private:
            uint64_t state_{};
        };

        // A random permutation of the indices [0, count) that moves every one
        // of them. Rejection sampling finds one within a few shuffles; the
        // rotation fallback only guards the loop.
        auto derangement(size_t count, ShuffleRng& rng) -> std::vector<size_t>
        {
            std::vector<size_t> order(count);
            for (size_t index = 0; index < count; ++index)
            {
                order[index] = index;
            }
            if (count < minimum_shuffle_candidates)
            {
                return order;
            }
            for (int attempt = 0; attempt < music_assignment_attempt_limit; ++attempt)
            {
                for (size_t index = count - 1; index > 0; --index)
                {
                    std::swap(order[index], order[rng.below(index + 1)]);
                }
                bool moved_all = true;
                for (size_t index = 0; index < count && moved_all; ++index)
                {
                    moved_all = order[index] != index;
                }
                if (moved_all)
                {
                    return order;
                }
            }
            for (size_t index = 0; index < count; ++index)
            {
                order[index] = (index + 1) % count;
            }
            return order;
        }

        // A world with more variants than its target reuses the target's
        // tracks in order, so areas that sounded different still map
        // deterministically.
        auto map_variants(std::unordered_map<std::wstring, std::wstring>& swaps,
                          const std::vector<std::wstring_view>& originals,
                          const std::vector<std::wstring_view>& replacements) -> void
        {
            for (size_t index = 0; index < originals.size(); ++index)
            {
                swaps[std::wstring{originals[index]}] =
                    std::wstring{replacements[index % replacements.size()]};
            }
        }
    } // namespace

    auto bgm_shuffle_worlds() -> const std::vector<BgmShuffleWorld>&
    {
        return music_worlds();
    }

    auto bgm_shuffle_groups() -> const std::vector<BgmShuffleGroup>&
    {
        return music_groups();
    }

    auto bgm_shuffle_group_of(std::wstring_view track) -> const BgmShuffleGroup*
    {
        for (const BgmShuffleGroup& group : music_groups())
        {
            if (std::ranges::find(group.tracks, track) != group.tracks.end())
            {
                return &group;
            }
        }
        return nullptr;
    }

    namespace
    {
        // The world or group a track belongs to; empty for a custom track.
        auto world_code_of(std::wstring_view track) -> std::wstring_view
        {
            if (const BgmShuffleGroup* const group = bgm_shuffle_group_of(track))
            {
                return group->code;
            }
            for (const BgmShuffleWorld& world : music_worlds())
            {
                for (const auto* list : {&world.fields, &world.battles})
                {
                    for (const std::wstring_view candidate : *list)
                    {
                        if (candidate == track)
                        {
                            return world.code;
                        }
                    }
                }
            }
            return {};
        }

        // A random one-to-one map of every field, battle and group track
        // into one pool of those tracks and the custom ones: a world's track
        // never lands in its own world, a group track anywhere but on
        // itself. A shuffle rarely meets every limit at
        // once, so each misplaced track then trades places with one that
        // fits there and takes it in return; the plain derangement fallback
        // only guards the loop.
        auto build_chaos_swaps(ShuffleRng& rng, const BgmCustomPools& custom)
            -> std::unordered_map<std::wstring, std::wstring>
        {
            std::vector<std::wstring> originals{};
            for (const BgmShuffleWorld& world : music_worlds())
            {
                originals.insert(originals.end(), world.fields.begin(), world.fields.end());
                originals.insert(originals.end(), world.battles.begin(), world.battles.end());
            }
            // Custom tracks only land on these: the custom player leaves the
            // group tracks' slots alone.
            const size_t world_originals = originals.size();
            for (const BgmShuffleGroup& group : music_groups())
            {
                originals.insert(originals.end(), group.tracks.begin(), group.tracks.end());
            }
            std::vector<std::wstring> pool{originals};
            std::unordered_map<std::wstring, const BgmCustomPoolTrack*> limits{};
            for (const BgmCustomPoolTrack& track : custom.tracks)
            {
                pool.push_back(track.entry);
                limits.emplace(track.entry, &track);
            }
            std::vector<std::wstring_view> original_worlds{};
            std::vector<bool> original_is_field{};
            for (const BgmShuffleWorld& world : music_worlds())
            {
                for (size_t index = 0; index < world.fields.size() + world.battles.size(); ++index)
                {
                    original_worlds.push_back(world.code);
                    original_is_field.push_back(index < world.fields.size());
                }
            }
            for (const BgmShuffleGroup& group : music_groups())
            {
                original_worlds.insert(original_worlds.end(), group.tracks.size(), group.code);
                original_is_field.insert(original_is_field.end(), group.tracks.size(), false);
            }
            // A track may land on an original: never in its own world, never
            // on itself, and a custom track only on a world's track, where
            // its limits allow.
            const auto fits = [&](const std::wstring& track, size_t index)
            {
                const auto limit = limits.find(track);
                if (limit == limits.end())
                {
                    return index >= world_originals
                               ? track != originals[index]
                               : world_code_of(track) != original_worlds[index];
                }
                const BgmCustomPoolTrack& custom_track = *limit->second;
                return index < world_originals &&
                       (original_is_field[index] ? custom_track.field : custom_track.battle) &&
                       (custom_track.worlds.empty() ||
                        std::ranges::find(custom_track.worlds, original_worlds[index]) !=
                            custom_track.worlds.end());
            };

            const auto shuffle = [&rng](std::vector<std::wstring>& tracks)
            {
                for (size_t index = tracks.size() - 1; index > 0; --index)
                {
                    std::swap(tracks[index], tracks[rng.below(index + 1)]);
                }
            };
            const auto assign = [&originals](const std::vector<std::wstring>& drawn)
            {
                std::unordered_map<std::wstring, std::wstring> swaps{};
                for (size_t index = 0; index < originals.size(); ++index)
                {
                    swaps[originals[index]] = drawn[index];
                }
                return swaps;
            };
            // Past the originals, the pool holds unused custom tracks: any
            // track may sit there.
            const auto fits_at = [&](const std::wstring& track, size_t index)
            {
                return index >= originals.size() || fits(track, index);
            };
            const auto repair = [&](std::vector<std::wstring>& drawn)
            {
                for (int pass = 0; pass < max_shuffle_repair_passes; ++pass)
                {
                    bool settled = true;
                    for (size_t index = 0; index < originals.size(); ++index)
                    {
                        if (fits(drawn[index], index))
                        {
                            continue;
                        }
                        settled = false;
                        const size_t start = rng.below(drawn.size());
                        for (size_t step = 0; step < drawn.size(); ++step)
                        {
                            const size_t other = (start + step) % drawn.size();
                            if (fits(drawn[other], index) && fits_at(drawn[index], other))
                            {
                                std::swap(drawn[index], drawn[other]);
                                break;
                            }
                        }
                    }
                    if (settled)
                    {
                        return true;
                    }
                }
                return false;
            };
            for (int attempt = 0; attempt < music_group_shuffle_attempt_limit; ++attempt)
            {
                shuffle(pool);
                if (repair(pool))
                {
                    return assign(pool);
                }
            }
            // Limits too tight to meet: fall back to the game's tracks alone.
            if (!custom.tracks.empty())
            {
                return build_chaos_swaps(rng, BgmCustomPools{});
            }
            for (int attempt = 0; attempt < music_assignment_attempt_limit; ++attempt)
            {
                shuffle(pool);
                bool moved = true;
                for (size_t index = 0; index < originals.size() && moved; ++index)
                {
                    moved = pool[index] != originals[index];
                }
                if (moved)
                {
                    return assign(pool);
                }
            }
            return {};
        }
    } // namespace

    auto build_bgm_swaps(BgmShuffleMode mode, std::wstring_view seed_identity,
                         const BgmCustomPools& custom)
        -> std::unordered_map<std::wstring, std::wstring>
    {
        std::unordered_map<std::wstring, std::wstring> swaps{};
        if (mode != BgmShuffleMode::Worlds && mode != BgmShuffleMode::BattleThemes &&
            mode != BgmShuffleMode::Chaos)
        {
            return swaps;
        }

        const auto& worlds = music_worlds();
        ShuffleRng rng{seed_identity};
        if (mode == BgmShuffleMode::Chaos)
        {
            return build_chaos_swaps(rng, custom);
        }
        const std::vector<size_t> field_targets = derangement(worlds.size(), rng);
        for (size_t index = 0; index < worlds.size(); ++index)
        {
            map_variants(swaps, worlds[index].fields, worlds[field_targets[index]].fields);
        }

        std::vector<size_t> battle_worlds{};
        for (size_t index = 0; index < worlds.size(); ++index)
        {
            if (!worlds[index].battles.empty())
            {
                battle_worlds.push_back(index);
            }
        }
        std::vector<size_t> battle_targets{};
        if (mode == BgmShuffleMode::Worlds)
        {
            for (const size_t world : battle_worlds)
            {
                battle_targets.push_back(field_targets[world]);
            }
        }
        else
        {
            for (const size_t position : derangement(battle_worlds.size(), rng))
            {
                battle_targets.push_back(battle_worlds[position]);
            }
        }
        for (size_t index = 0; index < battle_worlds.size(); ++index)
        {
            const BgmShuffleWorld& target = worlds[battle_targets[index]];
            map_variants(swaps, worlds[battle_worlds[index]].battles,
                         target.battles.empty() ? target.fields : target.battles);
        }

        // Drawn last, so the field and battle draws stay those of a seed
        // shuffled before the groups took part.
        for (const BgmShuffleGroup& group : music_groups())
        {
            const std::vector<size_t> targets = derangement(group.tracks.size(), rng);
            for (size_t index = 0; index < group.tracks.size(); ++index)
            {
                swaps[std::wstring{group.tracks[index]}] =
                    std::wstring{group.tracks[targets[index]]};
            }
        }

        std::erase_if(swaps,
                      [](const auto& entry)
                      {
                          return entry.first == entry.second;
                      });
        return swaps;
    }
} // namespace kh3ap
