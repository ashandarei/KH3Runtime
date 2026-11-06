#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeQualityOfLifeFlagKind
    {
        IntegerTransition,
        BooleanEnable,
    };

    struct RuntimeQualityOfLifeRule
    {
        std::wstring_view feature_name{};
        std::string_view slot_option{};
        std::wstring_view flag_name{};
        RuntimeQualityOfLifeFlagKind flag_kind{
                RuntimeQualityOfLifeFlagKind::IntegerTransition};
        int32_t trigger_value{};
        int32_t target_value{};
    };

    // These are the only QualityOfLifeActor rows that both belong to the
    // requested migration and carry a real game flag. The other requested
    // options are cooked-asset substitutions and never enter the actor's
    // Active && Gameflag != None branch.
    inline constexpr std::array<RuntimeQualityOfLifeRule, 2>
            runtime_quality_of_life_rules{{
                    {
                            L"DarkBaymaxPhase1Skip",
                            "dark_baymax_phase_1_skip",
                            L"gameflow_BX",
                            RuntimeQualityOfLifeFlagKind::IntegerTransition,
                            710,
                            811,
                    },
                    {
                            L"MaxLevelLeviathan",
                            "max_level_ship",
                            L"gameflow_CA_specialship_powerup",
                            RuntimeQualityOfLifeFlagKind::BooleanEnable,
                            -1,
                            -1,
                    },
            }};

    [[nodiscard]] constexpr auto should_apply_runtime_quality_of_life_rule(
            const RuntimeQualityOfLifeRule& rule,
            bool enabled,
            int32_t integer_value,
            bool boolean_value) noexcept -> bool
    {
        if (!enabled)
        {
            return false;
        }
        if (rule.flag_kind ==
            RuntimeQualityOfLifeFlagKind::IntegerTransition)
        {
            return integer_value == rule.trigger_value;
        }
        return !boolean_value;
    }
}
