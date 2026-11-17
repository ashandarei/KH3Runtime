#include "kh3ap/features/story/quality_of_life.h"

#include <cassert>

using namespace kh3ap;

int main()
{
    static_assert(runtime_quality_of_life_rules.size() == 7);

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
    for (size_t index = 2; index < 6; ++index)
    {
        const auto& piece = runtime_quality_of_life_rules[index];
        assert(piece.slot_option == "skip_collecting_kairi_heart_pieces");
        assert(piece.flag_name != L"gameflow_BT_DLC_07_KAIRI_05");
        assert(should_apply_runtime_quality_of_life_rule(piece, true, 0, false));
        assert(!should_apply_runtime_quality_of_life_rule(piece, false, 0, false));
        assert(!should_apply_runtime_quality_of_life_rule(piece, true, 0, true));
    }
    constexpr auto& scala = runtime_quality_of_life_rules[6];
    static_assert(scala.flag_name == L"gameflow_BT_DLC_07_BT_01");
    for (const int value : {0, 1, 100, 109})
    {
        assert(should_apply_runtime_quality_of_life_rule(scala, true, value, false));
        assert(!should_apply_runtime_quality_of_life_rule(scala, false, value, false));
    }
    for (const int value : {-1, 110, 120, 130, 9999})
    {
        assert(!should_apply_runtime_quality_of_life_rule(scala, true, value, false));
    }
    return 0;
}
