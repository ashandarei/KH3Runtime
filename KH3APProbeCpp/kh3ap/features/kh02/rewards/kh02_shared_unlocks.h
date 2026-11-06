#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::kh02rewards
{
    // Native save-data capabilities, distinct from each pawn's restrictions.
    constexpr uint16_t wall_run_mask = 0x0100;
    constexpr uint16_t style_change_mask = 0x0008;
    constexpr uint16_t mp_enabled_mask = 0x0004;
    constexpr uint16_t attraction_flow_mask = 0x0040;

    struct SharedUnlockOwnership
    {
        bool wall_run{}, style_change{}, magic{}, links{}, attraction_flow{};
    };

    struct SharedUnlockEffect
    {
        size_t setter_index;
        uint16_t mask;
        std::optional<bool> enabled;
    };

    using SharedUnlockPlan = std::array<SharedUnlockEffect, 4>;

    constexpr auto shared_unlock_plan(SharedUnlockOwnership owned, bool shared_style) -> SharedUnlockPlan
    {
        return {{{0, wall_run_mask, owned.wall_run},
                 {1, style_change_mask, shared_style ? std::optional{owned.style_change} : std::nullopt},
                 {2, mp_enabled_mask, owned.magic || owned.links},
                 {3, attraction_flow_mask, owned.attraction_flow}}};
    }

    constexpr auto shared_unlocks_match(uint16_t flags, const SharedUnlockPlan& plan) -> bool
    {
        for (const auto& effect : plan)
        {
            if (effect.enabled && ((flags & effect.mask) != 0) != *effect.enabled)
            {
                return false;
            }
        }
        return true;
    }

    template <typename Setter>
    auto apply_shared_unlock_plan(uint16_t flags, const SharedUnlockPlan& plan, Setter&& setter) -> bool
    {
        for (const auto& effect : plan)
        {
            if (effect.enabled && ((flags & effect.mask) != 0) != *effect.enabled)
            {
                if (!setter(effect.setter_index, *effect.enabled))
                {
                    return false;
                }
            }
        }
        return true;
    }

    // Borrowed current 0.2 hero only. Reads two shared native capability bytes.
    auto read_shared_unlock_flags(RC::Unreal::UObject* hero) -> std::optional<uint16_t>;
} // namespace kh3ap::kh02rewards
