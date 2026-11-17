#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeQualityOfLifeFlagKind
    {
        IntegerTransition,
        IntegerMinimum,
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

    // Native flag rules wake on save/map refresh or relevant flag changes.
    // Kairi's counter sums five Boolean flags; the extras story flag alone
    // enables Darkside but does not collect the four preceding fragments.
    // Leave KAIRI_05 and the main BT_DLC story progression to the real fight.
    inline constexpr std::array<RuntimeQualityOfLifeRule, 7>
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
                    {L"SkipKairiHeartPiece01", "skip_collecting_kairi_heart_pieces",
                     L"gameflow_BT_DLC_07_KAIRI_01", RuntimeQualityOfLifeFlagKind::BooleanEnable},
                    {L"SkipKairiHeartPiece02", "skip_collecting_kairi_heart_pieces",
                     L"gameflow_BT_DLC_07_KAIRI_02", RuntimeQualityOfLifeFlagKind::BooleanEnable},
                    {L"SkipKairiHeartPiece03", "skip_collecting_kairi_heart_pieces",
                     L"gameflow_BT_DLC_07_KAIRI_03", RuntimeQualityOfLifeFlagKind::BooleanEnable},
                    {L"SkipKairiHeartPiece04", "skip_collecting_kairi_heart_pieces",
                     L"gameflow_BT_DLC_07_KAIRI_04", RuntimeQualityOfLifeFlagKind::BooleanEnable},
                    {L"SkipKairiHeartCollection", "skip_collecting_kairi_heart_pieces",
                     L"gameflow_BT_DLC_07_BT_01", RuntimeQualityOfLifeFlagKind::IntegerMinimum,
                     0, 110},
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
        if (rule.flag_kind == RuntimeQualityOfLifeFlagKind::IntegerMinimum)
        {
            return integer_value >= rule.trigger_value && integer_value < rule.target_value;
        }
        return !boolean_value;
    }
}
