#include "kh3ap/features/kh02/story/kh02_mirror_travel_policy.h"
#include <cassert>
#include <stdexcept>

using namespace kh3ap::kh02_mirror_travel;

template <typename Action> void rejects(Action action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    assert(rejected);
}

int main()
{
    constexpr auto hub_world = L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin";
    constexpr auto hub_caller = L"/Game/Zevels/dw/dw_03/umap/dw_03_gameflow.Instance:Jump";
    for (const auto& room : kh02mirror_metadata::rooms)
    {
        const auto number = std::wstring(room.area < 10 ? L"0" : L"") + std::to_wstring(room.area);
        const auto destination = L"/Game/Zevels/dw/dw_" + number + L"/dw_" + number;
        for (int index = 0; index < room.start_count; ++index)
        {
            const auto plan = resolve(hub_caller, hub_world, destination, room.starts[index].tag, 3, false, false);
            assert(plan.package == room.package);
            assert(plan.tag == room.starts[index].tag);
            assert(plan.fade == 3);
            assert(!plan.special_battle_clear);
        }
        rejects([&] { resolve(hub_caller, hub_world, destination, L"unknown_tag", 0, false, false); });
        rejects([&] { resolve(hub_caller, hub_world, destination, room.starts[0].tag, 4, false, false); });
        rejects([&] { resolve(hub_caller, hub_world, destination, room.starts[0].tag, 0, false, true); });
    }

    constexpr auto hub_destination = L"/Game/Zevels/dw/dw_03/dw_03";
    constexpr auto phantom_world = L"/Game/Maps/APKH02Mirrors/APKH02Phantom.APKH02Phantom";
    constexpr auto phantom_caller = L"/Game/Zevels/dw/dw_07/umap/dw_07_gameflow.Instance:Jump";
    const auto cleared =
        resolve(phantom_caller, phantom_world, hub_destination, L"dw_03_Lv_ChallengeClear", 0, false, false);
    assert(cleared.special_battle_clear);
    rejects([&] { resolve(phantom_caller, hub_world, hub_destination, L"dw_03_Lv_ChallengeClear", 0, false, false); });
    rejects([&] { resolve(hub_caller, phantom_world, hub_destination, L"dw_03_Lv_ChallengeClear", 0, false, false); });
    rejects([&] { resolve(hub_caller, L"", hub_destination, L"dw_03_Lv_ChallengeClear", 0, false, false); });

    constexpr auto epilogue_world = L"/Game/Maps/APKH02Mirrors/APKH02Epilogue.APKH02Epilogue";
    constexpr auto epilogue_caller = L"/Game/Zevels/dw/dw_60/dw_60.Instance:Jump";
    for (const auto* destination : {L"/Game/Zaps/UI/BattleReport_Beginner", L"/Game/Zaps/UI/BattleReport_Standard",
                                    L"/Game/Zaps/UI/BattleReport_Proud"})
    {
        rejects(
            [&]
            { resolve(epilogue_caller, epilogue_world, destination, L"BattleReport_Lv_Start_01", 0, false, false); });
        const auto ended =
            resolve(epilogue_caller, epilogue_world, destination, L"BattleReport_Lv_Start_01", 0, true, false);
        assert(ended.package == L"/Game/Maps/GardenOfAssemblage/umap/GoA_01");
        assert(ended.tag == L"NormalSpawn");
        assert(!ended.special_battle_clear);
    }
}
