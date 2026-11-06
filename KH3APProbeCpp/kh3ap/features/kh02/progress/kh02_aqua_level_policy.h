#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace kh3ap::kh02_level
{
    inline constexpr int first_level = 50;
    inline constexpr int last_level = 99;
    inline constexpr int initial_exp = 441592;
    inline constexpr int maximum_exp = 2634752;
    inline constexpr int maximum_sora_exp = 9999999;
    inline constexpr auto exp_flag = L"gameflow_DW_AP_AQUA_EXP";
    inline constexpr std::string_view normalized_exp_flag = "gameflow_dw_ap_aqua_exp";

    struct Row
    {
        int experience{};
        int strength{};
        int magic{};
        int defense{};
        int ap{};
    };

    using Curve = std::array<Row, last_level - first_level + 1>;

    // Convert the active enemy's award to the same enemy at the recipient's
    // battle level. Preserve zero-EXP awards and bound native integer arithmetic.
    inline auto scale_award(int amount, int actual_base, int recipient_base) -> int
    {
        if (amount < 0 || actual_base <= 0 || recipient_base <= 0)
        {
            throw std::runtime_error("kh02_shared_experience_scale");
        }
        if (!amount)
        {
            return 0;
        }
        const auto scaled = (static_cast<int64_t>(amount) * recipient_base + actual_base / 2) / actual_base;
        return static_cast<int>(std::clamp<int64_t>(scaled, 1, maximum_sora_exp));
    }

    inline void validate(const Curve& curve)
    {
        if (curve.front().experience != initial_exp || curve.back().experience != maximum_exp)
        {
            throw std::runtime_error("kh02_aqua_level_curve_bounds");
        }
        int previous = initial_exp - 1;
        for (const auto& row : curve)
        {
            if (row.experience <= previous || row.experience > maximum_exp || row.strength < 1 || row.magic < 1 ||
                row.defense < 1 || row.ap < 0 || row.strength > 999 || row.magic > 999 || row.defense > 999 ||
                row.ap > 999)
            {
                throw std::runtime_error("kh02_aqua_level_curve_row");
            }
            previous = row.experience;
        }
    }

    inline auto add_experience(int total, int award) -> int
    {
        if (total < initial_exp || total > maximum_exp || award < 0)
        {
            throw std::runtime_error("kh02_aqua_experience_range");
        }
        return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(total) + award, maximum_exp));
    }

    inline auto level(const Curve& curve, int total) -> int
    {
        (void)add_experience(total, 0);
        const auto next = std::upper_bound(curve.begin(), curve.end(), total,
                                           [](int experience, const Row& row) { return experience < row.experience; });
        return first_level + static_cast<int>(next - curve.begin()) - 1;
    }
} // namespace kh3ap::kh02_level
