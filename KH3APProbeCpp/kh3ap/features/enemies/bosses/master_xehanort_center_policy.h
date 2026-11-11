#pragma once
#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace kh3ap
{
    using MasterArenaCenter = std::array<float, 3>;
    inline constexpr std::wstring_view data_master_xehanort_class =
        L"/Game/Blueprints/Enemy/e_ex367/e_ex367_Pawn.e_ex367_Pawn_C";
    inline constexpr std::wstring_view data_master_ascension_class =
        L"/Game/Blueprints/Enemy/e_ex367/e_ex367_AscensionPawn.e_ex367_AscensionPawn_C";
    inline constexpr std::wstring_view data_master_dark_shooting_class =
        L"/Game/Blueprints/Enemy/e_ex367/BT/"
        L"e_ex367_Attack3_DarkShooting_TD.e_ex367_Attack3_DarkShooting_TD_C";

    inline auto needs_master_arena_center(std::wstring_view source, std::wstring_view target)
        -> bool
    {
        return !source.empty() && source != target && target == data_master_xehanort_class;
    }

    // These fields are authored relative to the original arena's zero origin.
    // Always start from the CDO offset, including on repeated appearance/Retry.
    inline auto translated_master_arena_center(const MasterArenaCenter& arena,
                                               const MasterArenaCenter& authored)
        -> std::optional<MasterArenaCenter>
    {
        constexpr float limit = 10000000.0f;
        MasterArenaCenter result{};
        for (size_t index = 0; index < result.size(); ++index)
        {
            result[index] = arena[index] + authored[index];
            if (!std::isfinite(arena[index]) || !std::isfinite(authored[index]) ||
                !std::isfinite(result[index]) || std::abs(arena[index]) > limit ||
                std::abs(authored[index]) > limit || std::abs(result[index]) > limit)
            {
                return std::nullopt;
            }
        }
        return result;
    }

    // Keep the attack's angle and airborne height. Only shorten its horizontal
    // displacement when the host arena has a closer wall.
    inline auto constrain_master_warp_xy(const MasterArenaCenter& center,
                                         const MasterArenaCenter& destination,
                                         float permitted_radius) -> std::optional<MasterArenaCenter>
    {
        if (!translated_master_arena_center(center, {}) ||
            !translated_master_arena_center(destination, {}) || !std::isfinite(permitted_radius) ||
            permitted_radius < 0)
        {
            return std::nullopt;
        }
        const float dx = destination[0] - center[0];
        const float dy = destination[1] - center[1];
        const float distance = std::hypot(dx, dy);
        if (distance <= permitted_radius)
        {
            return destination;
        }
        const float scale = permitted_radius / distance;
        return MasterArenaCenter{center[0] + dx * scale, center[1] + dy * scale, destination[2]};
    }
} // namespace kh3ap
