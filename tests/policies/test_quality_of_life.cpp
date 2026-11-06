#include "kh3ap/features/story/quality_of_life.h"

#include <cassert>

using namespace kh3ap;

int main()
{
    static_assert(runtime_quality_of_life_rules.size() == 2);

    constexpr auto& dark_baymax = runtime_quality_of_life_rules[0];
    static_assert(dark_baymax.feature_name == L"DarkBaymaxPhase1Skip");
    static_assert(dark_baymax.slot_option == "dark_baymax_phase_1_skip");
    static_assert(dark_baymax.flag_name == L"gameflow_BX");
    static_assert(
            dark_baymax.flag_kind ==
            RuntimeQualityOfLifeFlagKind::IntegerTransition);
    static_assert(dark_baymax.trigger_value == 710);
    static_assert(dark_baymax.target_value == 811);
    assert(should_apply_runtime_quality_of_life_rule(
            dark_baymax,
            true,
            710,
            false));
    assert(!should_apply_runtime_quality_of_life_rule(
            dark_baymax,
            false,
            710,
            false));
    assert(!should_apply_runtime_quality_of_life_rule(
            dark_baymax,
            true,
            709,
            false));
    assert(!should_apply_runtime_quality_of_life_rule(
            dark_baymax,
            true,
            811,
            false));

    constexpr auto& max_leviathan = runtime_quality_of_life_rules[1];
    static_assert(max_leviathan.feature_name == L"MaxLevelLeviathan");
    static_assert(max_leviathan.slot_option == "max_level_ship");
    static_assert(
            max_leviathan.flag_name ==
            L"gameflow_CA_specialship_powerup");
    static_assert(
            max_leviathan.flag_kind ==
            RuntimeQualityOfLifeFlagKind::BooleanEnable);
    assert(should_apply_runtime_quality_of_life_rule(
            max_leviathan,
            true,
            0,
            false));
    assert(!should_apply_runtime_quality_of_life_rule(
            max_leviathan,
            true,
            0,
            true));
    assert(!should_apply_runtime_quality_of_life_rule(
            max_leviathan,
            false,
            0,
            false));
    return 0;
}
