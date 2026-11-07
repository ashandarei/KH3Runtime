#pragma once
#include "kh3ap/features/player/player_ids.h"
#include "kh02_enemy_scaling_policy.h"
#include "kh3ap/features/grants/battle_level/battle_level_progress.h"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace kh3ap
{
    struct Kh02EnemyHealthProfile
    {
        double fraction{1};
        int hit_points{};
        int maximum{};

        void observe(int health, int limit)
        {
            if (limit <= 0 || health < 0 || health > limit)
            {
                throw std::runtime_error("kh02_profile_enemy_health_range");
            }
            if (maximum == limit && health == hit_points)
            {
                return;
            }
            if (maximum == limit && health > 0 && health < limit)
            {
                fraction += double(health - hit_points) / limit;
            }
            else
            {
                fraction = double(health) / limit;
            }
            fraction = std::clamp(fraction, 0.0, 1.0);
            hit_points = health;
            maximum = limit;
        }

        int rescale(int limit)
        {
            if (limit <= 0)
            {
                throw std::runtime_error("kh02_profile_enemy_health_range");
            }
            maximum = limit;
            // Retain the unrounded fraction across swaps. Rounding each new
            // displayed HP back into a fraction would gradually heal enemies.
            hit_points = fraction > 0 ? std::clamp(int(std::ceil(fraction * limit - 1e-9)), 1, limit) : 0;
            return hit_points;
        }
    };

    struct Kh02BattleLevelSettings
    {
        bool enabled{true};
        int offset{};
    };

    inline constexpr int kh02_aqua_battle_level = 50;

    inline auto kh02_hero_battle_level(int native_level, int player_id, std::optional<int64_t> sora_level,
                                       Kh02BattleLevelSettings settings, bool final_boss) -> std::optional<int>
    {
        if (native_level < 1 || native_level > kh02_maximum_battle_level)
        {
            return std::nullopt;
        }
        // Preserve the original level-50 enemy balance for Aqua. Her independent
        // level growth must make her stronger, not rescale enemies after each level.
        // The KH3 host map's authored battle level can still be 2 in Castle Town.
        if (player_id == player_id_value(PlayerId::Aqua))
        {
            return kh02_aqua_battle_level;
        }
        if (final_boss || !settings.enabled)
        {
            return native_level;
        }
        if (player_id != player_id_value(PlayerId::Sora) || !sora_level || *sora_level < 1 || *sora_level > 100)
        {
            return std::nullopt;
        }
        const auto target = resolve_battle_level_target(static_cast<uint64_t>(*sora_level), settings.offset);
        return target ? std::optional<int>{static_cast<int>(std::min<uint64_t>(*target, kh02_maximum_battle_level))}
                      : std::nullopt;
    }

    // A zero-level startup notification is valid, but must not scale source rows.
    inline auto kh02_observed_battle_level(std::optional<int64_t> value) -> std::optional<int>
    {
        if (!value || *value < 0 || *value > kh02_maximum_battle_level)
        {
            throw std::runtime_error("kh02_stats_level_signature");
        }
        if (!*value)
        {
            return std::nullopt;
        }
        return static_cast<int>(*value);
    }
} // namespace kh3ap
