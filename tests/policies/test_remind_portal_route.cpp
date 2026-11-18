#include "kh3ap/features/story/remind/remind_portal_route.h"
#include "kh3ap/features/story/remind/yozora_return_route.h"
#include "kh3ap/features/story/remind/remind_return_route.h"
#include <cassert>
#include <limits>
#include <vector>

int main()
{
    using namespace kh3ap;
    // Actual loaded EGS kg_05_gimmick call bytes, captured 2026-09-22.
    const std::vector<uint8_t> original{
        0x1c, 0x10, 0xab, 0x7e, 0x80, 0xff, 0x01, 0x00, 0x00,
        0x21, 0xe7, 0xd9, 0x01, 0x00, 0xe7, 0xd9, 0x01, 0x00, 0, 0, 0, 0,
        0x21, 0x5e, 0x55, 0x02, 0x00, 0x5e, 0x55, 0x02, 0x00, 0, 0, 0, 0,
        0x24, 0x01, 0x16, 0x4d};
    constexpr uintptr_t target = 0x1ff807eab10;
    const PortalScriptName from_map{0x1d9e7, 0x1d9e7, 0}, from_tag{0x2555e, 0x2555e, 0};
    const PortalScriptName to_map{500001, 500001, 0}, to_tag{500002, 500002, 0};
    const auto patch = [&](std::vector<uint8_t>& bytes, std::wstring_view caller = remind_portal_caller,
                           size_t offset = 9, uintptr_t function = 0x1ff807eab10) {
        return redirect_remind_portal_bytecode(caller, bytes.data(), bytes.size(), offset,
                function, from_map, from_tag, to_map, to_tag);
    };
    auto bytes = original;
    assert(patch(bytes) == RemindPortalRouteResult::applied);
    auto expected = original;
    std::memcpy(expected.data() + 10, to_map.data(), 12);
    std::memcpy(expected.data() + 23, to_tag.data(), 12);
    assert(bytes == expected); // Only the destination pair changes, including the trailing instruction.
    assert(patch(bytes) == RemindPortalRouteResult::already_applied);
    assert(bytes == expected);

    for (const auto caller : {
            L"Function /Game/Levels/kg/kg_02/umap/kg_02_gimmick.kg_02_gimmick_C:ExecuteUbergraph_kg_02_gimmick",
            L"Function /Game/Maps/GardenOfAssemblage/GoA_01.GoA_01_C:ExecuteUbergraph_GoA_01",
            L"Function /Script/TresGame.TresGameBlueprintLibrary:TresMapJump", L""})
    {
        bytes = original;
        assert(patch(bytes, caller) == RemindPortalRouteResult::ignored);
        assert(bytes == original);
    }
    // Every byte of the call must match before either destination is changed.
    for (size_t i = 0; i < original.size() - 1; ++i)
    {
        bytes = original;
        bytes[i] ^= 0x40;
        const auto before = bytes;
        assert(patch(bytes) == RemindPortalRouteResult::rejected);
        assert(bytes == before);
    }
    for (size_t size = 0; size < original.size() - 1; ++size)
    {
        bytes.assign(original.begin(), original.begin() + size);
        const auto before = bytes;
        assert(patch(bytes) == RemindPortalRouteResult::rejected);
        assert(bytes == before);
    }
    bytes = original;
    assert(patch(bytes, remind_portal_caller, 8) == RemindPortalRouteResult::rejected);
    assert(patch(bytes, remind_portal_caller, std::numeric_limits<size_t>::max()) == RemindPortalRouteResult::rejected);
    assert(patch(bytes, remind_portal_caller, 9, target + 1) == RemindPortalRouteResult::rejected);
    assert(bytes == original);

    // Scala keeps the retail PlayerStart and fade when the return portal is confirmed.
    const auto scala_return = [&](std::wstring_view caller) {
        return redirect_map_jump_bytecode(caller, scala_return_caller, bytes.data(), bytes.size(), 9,
                target, from_map, from_tag, to_map, from_tag, 1);
    };
    assert(scala_return(remind_portal_caller) == RemindPortalRouteResult::ignored);
    assert(bytes == original);
    assert(scala_return(scala_return_caller) == RemindPortalRouteResult::applied);
    expected = original;
    std::memcpy(expected.data() + 10, to_map.data(), sizeof(to_map));
    assert(bytes == expected);
    assert(scala_return(scala_return_caller) == RemindPortalRouteResult::already_applied);
    assert(bytes == expected);

    // The retail Graveyard main gameflow uses the same Scala start tag with fade 2.
    const auto graveyard_departure = [&](std::wstring_view caller) {
        return redirect_map_jump_bytecode(caller, graveyard_scala_caller, bytes.data(), bytes.size(), 9,
                target, from_map, from_tag, to_map, from_tag, graveyard_scala_fade);
    };
    bytes = original;
    assert(graveyard_departure(graveyard_scala_caller) == RemindPortalRouteResult::rejected);
    assert(bytes == original);
    bytes[bytes.size() - 3] = 2;
    expected = bytes;
    assert(graveyard_departure(remind_portal_caller) == RemindPortalRouteResult::ignored);
    assert(bytes == expected);
    std::memcpy(expected.data() + 10, to_map.data(), sizeof(to_map));
    assert(graveyard_departure(graveyard_scala_caller) == RemindPortalRouteResult::applied);
    assert(bytes == expected);
    assert(graveyard_departure(graveyard_scala_caller) == RemindPortalRouteResult::already_applied);
    assert(bytes == expected);

    // EGS ss_01_gameflow's victory exit is TresMapJump(EndSave,
    // #FalsusRexClear, ByteConst(0)). Its loss exit uses #FalsusRexBadEnd.
    auto victory = original;
    victory[victory.size() - 3] = 0;
    const auto return_to_garden = [&](std::vector<uint8_t>& script,
                                      std::wstring_view caller = yozora_return_caller,
                                      size_t offset = 9, uintptr_t function = 0x1ff807eab10) {
        return redirect_yozora_return_bytecode(caller, script.data(), script.size(), offset,
                function, from_map, from_tag, to_map, to_tag);
    };
    bytes = victory;
    assert(return_to_garden(bytes) == RemindPortalRouteResult::applied);
    expected = victory;
    std::memcpy(expected.data() + 10, to_map.data(), 12);
    std::memcpy(expected.data() + 23, to_tag.data(), 12);
    assert(bytes == expected);
    assert(return_to_garden(bytes) == RemindPortalRouteResult::already_applied);
    assert(bytes == expected);

    for (const auto caller : {remind_portal_caller,
            std::wstring_view(L"Function /Game/Levels/rg_DLC/rg_03/umap/rg_03_gameflow."
                               L"rg_03_gameflow_C:ExecuteUbergraph_rg_03_gameflow"),
            std::wstring_view{}})
    {
        bytes = victory;
        assert(return_to_garden(bytes, caller) == RemindPortalRouteResult::ignored);
        assert(bytes == victory);
    }
    // Reject the loss tag, other map/tag pairs, altered call layouts and fades
    // atomically. This also guards against changing ordinary data-fight travel.
    for (size_t i = 0; i < victory.size() - 1; ++i)
    {
        bytes = victory;
        bytes[i] ^= 0x40;
        const auto before = bytes;
        assert(return_to_garden(bytes) == RemindPortalRouteResult::rejected);
        assert(bytes == before);
    }
    for (size_t size = 0; size < victory.size() - 1; ++size)
    {
        bytes.assign(victory.begin(), victory.begin() + size);
        const auto before = bytes;
        assert(return_to_garden(bytes) == RemindPortalRouteResult::rejected);
        assert(bytes == before);
    }
    bytes = victory;
    assert(return_to_garden(bytes, yozora_return_caller, 8) == RemindPortalRouteResult::rejected);
    assert(return_to_garden(bytes, yozora_return_caller, std::numeric_limits<size_t>::max()) == RemindPortalRouteResult::rejected);
    assert(return_to_garden(bytes, yozora_return_caller, 9, target + 1) == RemindPortalRouteResult::rejected);
    assert(bytes == victory);
    assert(redirect_yozora_return_bytecode(yozora_return_caller, nullptr, victory.size(), 9,
            target, from_map, from_tag, to_map, to_tag) == RemindPortalRouteResult::rejected);

    // Both ss_51 ending branches use EndSave/#ReMindClear/ByteConst(0).
    // The same map is used by unrelated clears, which must remain untouched.
    const auto remind_return = [&](std::vector<uint8_t>& script,
                                    std::wstring_view caller = remind_return_caller) {
        return redirect_remind_return_bytecode(caller, script.data(), script.size(), 9,
            target, from_map, from_tag, to_map, to_tag);
    };
    bytes = victory;
    assert(remind_return(bytes) == RemindPortalRouteResult::applied);
    assert(bytes == expected);
    assert(remind_return(bytes) == RemindPortalRouteResult::already_applied);
    for (const auto caller : {yozora_return_caller, remind_portal_caller, std::wstring_view{}})
    {
        bytes = victory;
        assert(remind_return(bytes, caller) == RemindPortalRouteResult::ignored);
        assert(bytes == victory);
    }
    for (size_t i = 0; i < victory.size() - 1; ++i)
    {
        bytes = victory;
        bytes[i] ^= 0x40;
        const auto before = bytes;
        assert(remind_return(bytes) == RemindPortalRouteResult::rejected);
        assert(bytes == before);
    }
    for (size_t size = 0; size < victory.size() - 1; ++size)
    {
        bytes.assign(victory.begin(), victory.begin() + size);
        const auto before = bytes;
        assert(remind_return(bytes) == RemindPortalRouteResult::rejected);
        assert(bytes == before);
    }
}
