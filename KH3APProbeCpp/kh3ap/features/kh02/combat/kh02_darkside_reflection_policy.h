#pragma once
#include <cstdint>
#include <optional>

namespace kh3ap
{
    inline constexpr std::uint32_t kh02_projectile_reflected_state = 0x200;
    inline constexpr std::uint32_t kh02_projectile_teardown_state = 0x10;
    inline constexpr int kh02_darkside_original_projectile_team = 255;

    struct Kh02DarksideReflectionEvidence
    {
        std::uint32_t state{};
        int actor_team{}, collision_team{}, sora_team{};
        bool source_forest_context{}, current_sora_possessed{}, homing_target_is_owner{};
        bool ignores_current_sora{}, ignores_owner{};
    };

    // Neither an outgoing shot nor a reflection attributed to another actor
    // may acquire Sora's team. Leave existing non-source team changes alone.
    inline auto kh02_darkside_reflection_team(const Kh02DarksideReflectionEvidence& evidence) -> std::optional<int>
    {
        if (!evidence.source_forest_context || !evidence.current_sora_possessed || !evidence.homing_target_is_owner ||
            !evidence.ignores_current_sora || evidence.ignores_owner ||
            !(evidence.state & kh02_projectile_reflected_state) || (evidence.state & kh02_projectile_teardown_state) ||
            evidence.actor_team != kh02_darkside_original_projectile_team ||
            evidence.collision_team != evidence.actor_team || evidence.sora_team < 0 ||
            evidence.sora_team >= kh02_darkside_original_projectile_team)
        {
            return std::nullopt;
        }
        return evidence.sora_team;
    }
} // namespace kh3ap
