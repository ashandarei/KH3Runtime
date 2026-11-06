#include "kh3ap/features/story/progression/battlegate_activation.h"

#include <cassert>
#include <limits>
#include <string>

using namespace kh3ap;

int main()
{
    const auto& caribbean = battlegate_activation_rules[6];
    const auto owner = std::wstring(caribbean.owner_prefix) + L"ca_02_gameflow_C_0";
    assert(caribbean.matches(caribbean.caller, owner));
    assert(!caribbean.matches(L"Function /Script/TresGame.TresGameBlueprintLibrary:IsGameCleared", owner));
    assert(!caribbean.matches(caribbean.caller, L"Default__ca_02_gameflow_C"));
    assert(!caribbean.matches(caribbean.caller,
            L"ca_02_gameflow_C /Game/Levels/ca/ca_01/ca_01.ca_01:PersistentLevel.ca_02_gameflow_C_0"));
    const auto& corona = battlegate_activation_rules[3];
    assert(!battlegate_result_local(corona, L"CallFunc_IsGameCleared_ReturnValue2"));
    const auto& san_fransokyo = battlegate_activation_rules[7];
    assert(battlegate_result_local(san_fransokyo, L"CallFunc_IsGameCleared_ReturnValue2"));
    assert(battlegate_result_local(san_fransokyo, L"CallFunc_IsGameCleared_ReturnValue3"));
    assert(!battlegate_result_local(san_fransokyo, L"CallFunc_IsGameCleared_ReturnValue4"));

    // The hook must reject any unrelated use of IsGameCleared, changed local,
    // stale resolved pointer or truncated caller script. Test both stock branch
    // shapes: start-only in TT/TS and start/stop in CA/FZ/HE/RA/MI/BX/KG.
    constexpr uintptr_t getter = 0x100000001ULL, property = 0x100000002ULL;
    constexpr size_t offset = 10;
    std::array<uint8_t, 24> script{};
    const auto put = [&](size_t at, auto value) { std::memcpy(script.data() + at, &value, sizeof(value)); };
    script[0] = 0x1c; put(1, getter); script[9] = 0x16;
    script[10] = 0x07; put(11, uint32_t{1000}); script[15] = 0x00; put(16, property);
    const auto original = script;
    assert(is_battlegate_result_branch(script, offset, getter, property));
    assert(script == original);
    for (size_t length = 0; length < script.size(); ++length)
        assert(!is_battlegate_result_branch(std::span{script}.first(length), offset, getter, property));
    assert(!is_battlegate_result_branch(script, std::numeric_limits<size_t>::max(), getter, property));
    assert(!is_battlegate_result_branch(script, 0, getter, property));
    assert(!is_battlegate_result_branch(script, offset, getter + 1, property));
    assert(!is_battlegate_result_branch(script, offset, getter, property + 1));
    assert(!is_battlegate_result_branch(script, offset, 0, property));
    assert(!is_battlegate_result_branch(script, offset, getter, 0));
    for (const auto at : {0, 1, 9, 10, 15, 16})
    {
        script = original; script[at] ^= 0xff;
        assert(!is_battlegate_result_branch(script, offset, getter, property));
    }
    script = original; script[10] = 0x4f; script[11] = 0x00; put(12, property);
    assert(is_battlegate_result_branch(std::span{script}.first(20), offset, getter, property));
    assert(!is_battlegate_result_branch(std::span{script}.first(19), offset, getter, property));
}
