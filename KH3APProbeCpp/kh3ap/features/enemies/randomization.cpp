// SeedRandom and shuffle behavior ported from KH3's Python generator.
// See licenses/LICENSE-KH3RandomizerClient.txt (Apache-2.0).
#include <string_view>
#include "randomization.h"
#include "randomization_catalog.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace kh3ap::randomization
{
    using Json = nlohmann::json;
    namespace
    {
        constexpr auto utf8_ascii_limit = 0x80;
        constexpr auto utf8_three_byte_lead = 0xe0;
        constexpr auto utf8_four_byte_lead = 0xf0;
        constexpr auto utf8_three_byte_continuations = 2;
        constexpr auto utf8_four_byte_continuations = 3;
        constexpr auto utf8_continuation_payload_bits = 6;
        constexpr auto utf8_continuation_payload_mask = 63;
        constexpr auto utf16_max_single_unit = 0xffff;
        constexpr auto utf16_supplementary_plane_start = 0x10000;
        constexpr auto utf16_high_surrogate_first = 0xd800;
        constexpr auto utf16_low_surrogate_first = 0xdc00;
        constexpr auto utf16_surrogate_payload_bits = 10;
        constexpr auto utf16_surrogate_payload_mask = 1023;
        constexpr auto enemy_runtime_format_version = 2;

        constexpr auto seed_hash_multiplier = 31;
        constexpr auto seed_random_state_size = 56;
        constexpr auto seed_random_active_state_size = 55;
        constexpr auto seed_random_initial_cursor = 21;
        constexpr auto seed_random_seed_constant = 161803398;
        constexpr auto seed_random_warmup_passes = 4;
        constexpr auto seed_random_subtract_offset = 30;
        constexpr int first_data_boss_id = 93;
        constexpr int last_data_boss_id = 105;
        constexpr int first_endgame_boss_id = 54;
        constexpr int last_endgame_boss_id = 58;
        constexpr int additional_endgame_boss_id = 77;
        constexpr int yozora_boss_id = 106;

        class SeedRandom
        {
            std::array<int32_t, seed_random_state_size> state{};
            int a{}, b{seed_random_initial_cursor};
            static int32_t subtract(int32_t x, int32_t y)
            {
                return std::bit_cast<int32_t>(static_cast<uint32_t>(x) - static_cast<uint32_t>(y));
            }

        public:
            explicit SeedRandom(const std::string& seed)
            {
                uint32_t hashed{};
                // Hash UTF-16 code units, including surrogate pairs, on every platform.
                for (size_t i = 0; i < seed.size();)
                {
                    uint32_t value = static_cast<unsigned char>(seed[i++]);
                    int extra = value < utf8_ascii_limit       ? 0
                                : value < utf8_three_byte_lead ? 1
                                : value < utf8_four_byte_lead  ? utf8_three_byte_continuations
                                                               : utf8_four_byte_continuations;
                    if (extra)
                    {
                        value &= (1U << (utf8_continuation_payload_bits - extra)) - 1;
                    }
                    while (extra--)
                    {
                        if (i == seed.size())
                        {
                            throw std::runtime_error("Invalid seed UTF-8");
                        }
                        value = (value << utf8_continuation_payload_bits) |
                                (static_cast<unsigned char>(seed[i++]) &
                                 utf8_continuation_payload_mask);
                    }
                    if (value > utf16_max_single_unit)
                    {
                        value -= utf16_supplementary_plane_start;
                        hashed = seed_hash_multiplier * hashed + utf16_high_surrogate_first +
                                 (value >> utf16_surrogate_payload_bits);
                        value = utf16_low_surrogate_first + (value & utf16_surrogate_payload_mask);
                    }
                    hashed = seed_hash_multiplier * hashed + value;
                }
                const auto signed_hash = std::bit_cast<int32_t>(hashed);
                const int64_t magnitude = signed_hash == INT32_MIN
                                              ? INT32_MAX
                                              : std::abs(static_cast<int64_t>(signed_hash));
                int32_t mj = static_cast<int32_t>(seed_random_seed_constant - magnitude);
                state[seed_random_active_state_size] = mj;
                int32_t mk = 1;
                for (int i = 1; i < seed_random_active_state_size; ++i)
                {
                    const int index =
                        (seed_random_initial_cursor * i) % seed_random_active_state_size;
                    state[index] = mk;
                    mk = subtract(mj, mk);
                    if (mk < 0)
                    {
                        mk += INT32_MAX;
                    }
                    mj = state[index];
                }
                for (int pass = 0; pass < seed_random_warmup_passes; ++pass)
                {
                    for (int i = 1; i < seed_random_state_size; ++i)
                    {
                        state[i] = subtract(state[i], state[1 + (i + seed_random_subtract_offset) %
                                                                    seed_random_active_state_size]);
                        if (state[i] < 0)
                        {
                            state[i] += INT32_MAX;
                        }
                    }
                }
            }
            size_t next(size_t count)
            {
                if (!count)
                {
                    throw std::runtime_error("Empty native randomization pool");
                }
                a = a < seed_random_active_state_size ? a + 1 : 1;
                b = b < seed_random_active_state_size ? b + 1 : 1;
                int32_t value = subtract(state[a], state[b]);
                if (value == INT32_MAX)
                {
                    --value;
                }
                if (value < 0)
                {
                    value += INT32_MAX;
                }
                state[a] = value;
                return static_cast<size_t>(value * (1.0 / INT32_MAX) * count);
            }
            template <class T> void shuffle(std::vector<T>& values)
            {
                for (size_t i = values.size(); i > 1; --i)
                {
                    std::swap(values[i - 1], values[next(i)]);
                }
            }
        };

        bool category_compatible(const std::string& source, const std::string& target)
        {
            return source == target || (source == "flying" && target == "ground");
        }

        Json enemies(const Json& catalog, const std::string& seed, const Json& config)
        {
            const auto& groups = catalog.at("groups");
            std::map<std::string, Json> rules;
            for (const auto& rule : catalog.at("rules"))
            {
                rules.emplace(rule.at("path"), rule);
            }
            SeedRandom rng(seed), chaos_rng(seed);
            std::map<std::string, std::string> lookup;
            const auto shuffle_pool = [&](bool mini)
            {
                std::vector<std::string> paths;
                for (const auto& group : groups)
                {
                    if (group.at("boss") == true || group.at("mini") != mini)
                    {
                        continue;
                    }
                    const std::string path = group.at("path");
                    if (std::find(paths.begin(), paths.end(), path) == paths.end())
                    {
                        paths.push_back(path);
                    }
                }
                for (const std::string category :
                     {"ground", "flying", "aquatic", "stationary", "large"})
                {
                    std::vector<std::string> sources, candidates;
                    for (const auto& path : paths)
                    {
                        const auto& rule = rules.at(path);
                        if (rule.at("mini") != mini)
                        {
                            throw std::runtime_error("Invalid native enemy catalog");
                        }
                        if (rule.at("category") == category)
                        {
                            if (rule.at("pinned") == true)
                            {
                                lookup[path] = path;
                            }
                            else
                            {
                                sources.push_back(path);
                            }
                        }
                        if (rule.at("pinned") == false &&
                            category_compatible(category, rule.at("category")))
                        {
                            candidates.push_back(path);
                        }
                    }
                    rng.shuffle(candidates);
                    if (candidates.size() < sources.size())
                    {
                        throw std::runtime_error("Incomplete native enemy pool");
                    }
                    for (size_t i = 0; i < sources.size(); ++i)
                    {
                        lookup[sources[i]] = candidates[i];
                    }
                }
            };
            shuffle_pool(true);
            std::vector<size_t> boss_groups;
            for (size_t i = 0; i < groups.size(); ++i)
            {
                const auto& group = groups[i];
                if (group.at("boss") == true &&
                    !(config.at("keep_data") == true && group.at("data") == true) &&
                    !(config.at("keep_endgame") == true && group.at("endgame") == true) &&
                    !(config.at("keep_yozora") == true && group.at("yozora") == true))
                {
                    boss_groups.push_back(i);
                }
            }
            auto shuffled_bosses = boss_groups;
            rng.shuffle(shuffled_bosses);
            std::map<size_t, size_t> boss_lookup;
            for (size_t i = 0; i < boss_groups.size(); ++i)
            {
                boss_lookup[boss_groups[i]] = shuffled_bosses[i];
            }
            shuffle_pool(false);
            std::vector<std::string> packages, classes, entries;
            const auto index = [](std::vector<std::string>& values, const std::string& value)
            {
                auto found = std::find(values.begin(), values.end(), value);
                if (found != values.end())
                {
                    return static_cast<size_t>(found - values.begin());
                }
                values.push_back(value);
                return values.size() - 1;
            };
            for (size_t i = 0; i < groups.size(); ++i)
            {
                const auto& group = groups[i];
                if (group.at("excluded") == true ||
                    (group.at("boss") == true && !boss_lookup.contains(i)))
                {
                    continue;
                }
                const std::string source = group.at("path");
                std::string replacement =
                    group.at("boss") == true
                        ? groups[boss_lookup.at(i)].at("path").get<std::string>()
                        : lookup.at(source);
                std::vector<std::string> chaos_pool;
                if (config.at("enemy_chaos") == true && rules.contains(source) &&
                    rules.at(source).at("pinned") == false)
                {
                    const auto& rule = rules.at(source);
                    for (const auto& candidate : catalog.at("rules"))
                    {
                        if (candidate.at("pinned") == false &&
                            candidate.at("mini") == rule.at("mini") &&
                            category_compatible(rule.at("category"), candidate.at("category")))
                        {
                            chaos_pool.push_back(candidate.at("path"));
                        }
                    }
                }
                const size_t package = index(packages, group.at("package"));
                for (const std::string actor : group.at("actors"))
                {
                    if (!chaos_pool.empty())
                    {
                        replacement = chaos_pool[chaos_rng.next(chaos_pool.size())];
                    }
                    entries.push_back(std::to_string(package) + "|" + actor + "|" +
                                      std::to_string(index(classes, replacement)));
                }
            }
            return Json{{"enemy_runtime_format_version", enemy_runtime_format_version},
                        {"entry_count", entries.size()},
                        {"enemy_runtime_packages", packages},
                        {"enemy_runtime_classes", classes},
                        {"enemy_runtime_entries", entries}};
        }

        Json bosses(const Json& catalog, const std::string& seed, const Json& config)
        {
            const auto& rows = catalog.at("bosses");
            std::set<std::string> blocked_ids =
                config.at("boss_blacklist").get<std::set<std::string>>();
            std::set<std::string> blocked_pawns;
            std::vector<size_t> eligible;
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const auto& row = rows[i];
                const std::string id = row.at("id");
                if (blocked_ids.erase(id))
                {
                    blocked_pawns.insert(row.at("pawn"));
                }
                const int numeric = std::stoi(id.substr(std::string_view{"BOSS_"}.size()));
                if (row.at("pinned") == true ||
                    (config.at("keep_data") == true && numeric >= first_data_boss_id &&
                     numeric <= last_data_boss_id) ||
                    (config.at("keep_endgame") == true &&
                     ((numeric >= first_endgame_boss_id && numeric <= last_endgame_boss_id) ||
                      numeric == additional_endgame_boss_id)) ||
                    (config.at("keep_yozora") == true && numeric == yozora_boss_id))
                {
                    continue;
                }
                eligible.push_back(i);
            }
            if (!blocked_ids.empty())
            {
                throw std::runtime_error("Unknown native boss blacklist ID");
            }
            auto replacements = eligible;
            SeedRandom rng(seed);
            rng.shuffle(replacements);
            const auto compatible = [&](size_t source, size_t target)
            {
                const auto& allowed = rows[source].at("allowed");
                return std::find(allowed.begin(), allowed.end(), rows[target].at("id")) !=
                       allowed.end();
            };
            std::map<std::string, size_t> allowed_pawns;
            for (size_t i : eligible)
            {
                if (!blocked_pawns.contains(rows[i].at("pawn")))
                {
                    allowed_pawns.emplace(rows[i].at("pawn"), i);
                }
            }
            for (size_t i = 0; i < eligible.size(); ++i)
            {
                if (!blocked_pawns.empty())
                {
                    if (!blocked_pawns.contains(rows[replacements[i]].at("pawn")) &&
                        compatible(eligible[i], replacements[i]))
                    {
                        continue;
                    }
                    std::vector<size_t> candidates;
                    for (const auto& [pawn, target] : allowed_pawns)
                    {
                        if (compatible(eligible[i], target))
                        {
                            candidates.push_back(target);
                        }
                    }
                    if (candidates.empty())
                    {
                        throw std::runtime_error(
                            "Boss blacklist leaves no compatible replacement for " +
                            rows[eligible[i]].at("id").get<std::string>());
                    }
                    replacements[i] = candidates[rng.next(candidates.size())];
                }
                else if (!compatible(eligible[i], replacements[i]))
                {
                    std::vector<size_t> candidates;
                    for (size_t other = 0; other < eligible.size(); ++other)
                    {
                        if (other != i && compatible(eligible[i], replacements[other]) &&
                            compatible(eligible[other], replacements[i]))
                        {
                            candidates.push_back(other);
                        }
                    }
                    if (candidates.empty())
                    {
                        throw std::runtime_error("No compatible native boss swap");
                    }
                    std::swap(replacements[i],
                              replacements[candidates[rng.next(candidates.size())]]);
                }
            }
            std::vector<std::string> classes, entries;
            const auto index = [&](const std::string& path)
            {
                const auto found = std::find(classes.begin(), classes.end(), path);
                if (found != classes.end())
                {
                    return static_cast<size_t>(found - classes.begin());
                }
                classes.push_back(path);
                return classes.size() - 1;
            };
            for (size_t i = 0; i < eligible.size(); ++i)
            {
                const auto& source = rows[eligible[i]];
                const auto& target = rows[replacements[i]];
                if (source.at("pawn") == target.at("pawn"))
                {
                    continue;
                }
                const size_t source_index = index(source.at("path"));
                const size_t target_index = index(target.at("path"));
                entries.push_back(source.at("id").get<std::string>() + "|" +
                                  std::to_string(source_index) + "|" +
                                  std::to_string(target_index));
            }
            return Json{{"boss_runtime_format_version", 1},
                        {"boss_runtime_entry_count", entries.size()},
                        {"boss_runtime_classes", classes},
                        {"boss_runtime_entries", entries}};
        }
    } // namespace

    auto validate_settings(const Json& settings) -> void
    {
        if (settings.at("schema") != "kh3ap.seed_settings.v1" ||
            !settings.at("slot").is_number_integer() || settings.at("slot").get<int64_t>() <= 0 ||
            !settings.at("slot_name").is_string() ||
            settings.at("slot_name").get<std::string>().empty())
        {
            throw std::runtime_error("Invalid native seed identity");
        }
        for (const auto field :
             {"enemy_randomization_initially_enabled", "boss_randomization_initially_enabled",
              "party_randomization_initially_enabled", "party_randomize_on_room_change"})
        {
            if (!settings.at(field).is_boolean())
            {
                throw std::runtime_error("Invalid native randomization default");
            }
        }
        const auto& config = settings.at("native_randomization");
        if (config.at("version") != 1)
        {
            throw std::runtime_error("Unsupported native randomization version");
        }
        for (const auto field : {"enemy_chaos", "keep_data", "keep_endgame", "keep_yozora"})
        {
            if (!config.at(field).is_boolean())
            {
                throw std::runtime_error("Invalid native randomization boolean");
            }
        }
        if (!config.at("boss_blacklist").is_array())
        {
            throw std::runtime_error("Invalid native boss blacklist");
        }
        const std::string seed = settings.at("seed");
        if (seed.empty())
        {
            throw std::runtime_error("Missing native randomization seed");
        }
    }

    auto generate(const Json& settings) -> Json
    {
        validate_settings(settings);
        const std::string seed = settings.at("seed");
        const auto& config = settings.at("native_randomization");
        static const Json catalog = Json::parse(catalog_data);
        return Json{{"enemy_runtime_remap", enemies(catalog, seed, config)},
                    {"boss_runtime_remap", bosses(catalog, seed, config)}};
    }
} // namespace kh3ap::randomization
