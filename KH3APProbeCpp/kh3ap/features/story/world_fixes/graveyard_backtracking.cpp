#include "kh3ap/features/story/world_fixes/graveyard_backtracking.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <UnrealDef.hpp>
#include <string>
#include <algorithm>

namespace kh3ap
{
    namespace
    {
        auto script_name(const wchar_t* text) -> GraveyardScriptName
        {
            const RC::Unreal::FName name(text, RC::Unreal::FNAME_Add);
            return {name.GetComparisonIndex(), name.GetDisplayIndex(), name.GetNumber()};
        }
    }

    auto try_apply_graveyard_backtracking(RC::Unreal::UObject* object, size_t variant) -> bool
    {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(object) || variant > 2) return false;
        const auto owner = variant == 0 ? L"kg_02_gameflow_Main" :
                variant == 1 ? L"kg_02_Mission_Area_A" : L"kg_02_Mission_Area_B";
        const auto prefix = std::wstring(L"/Game/Levels/kg/kg_02/umap/") + owner + L"." + owner + L"_C:";
        const auto ubergraph = std::wstring(L"ExecuteUbergraph_") + owner;
        auto* compare = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                L"/Script/TresGame.TresGameBlueprintLibrary:GreaterEqual_FlagLabel");
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        if (!is_uobject_valid_fast(compare) || compare->GetParmsSize() != 17 ||
                !is_uobject_valid_fast(library)) return false;
        auto* result = compare->GetReturnProperty();
        if (!result || !result->IsA<FBoolProperty>() || result->GetOffset_Internal() != 16) return false;

        const auto patch = [&](const std::wstring& event, const wchar_t* flag, const wchar_t* label, bool mission) {
            auto* fn = object->GetFunctionByNameInChain(event.c_str());
            if (!is_uobject_valid_fast(fn) || fn->GetPathName() != prefix + event) return false;
            auto& script = fn->GetScript();
            std::array<GraveyardEntryCondition, 2> conditions{{
                    {script_name(flag), script_name(label)},
                    {script_name(L"gameflow_KG"), script_name(L"KG_02_LABYRINTH_END")}}};
            auto bytes = guard_graveyard_entry(
                    {script.GetData(), static_cast<size_t>(script.Num())}, script_name(ubergraph.c_str()),
                    reinterpret_cast<uintptr_t>(compare), reinterpret_cast<uintptr_t>(library),
                    reinterpret_cast<uintptr_t>(result), {conditions.data(), mission ? 2u : 1u});
            // CDO and actor construction can both observe the same UFunction.
            if (bytes.empty())
            {
                const size_t prefix_size = mission ? 130 : 65;
                if (static_cast<size_t>(script.Num()) > prefix_size)
                {
                    const auto expected = guard_graveyard_entry(
                            {script.GetData() + prefix_size, static_cast<size_t>(script.Num()) - prefix_size},
                            script_name(ubergraph.c_str()), reinterpret_cast<uintptr_t>(compare),
                            reinterpret_cast<uintptr_t>(library), reinterpret_cast<uintptr_t>(result),
                            {conditions.data(), mission ? 2u : 1u});
                    if (expected.size() == static_cast<size_t>(script.Num()) &&
                            std::equal(expected.begin(), expected.end(), script.GetData())) return true;
                }
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] GraveyardBacktracking rejected event={} size={}\n"), event, script.Num());
                return false;
            }
            script.Empty();
            script.Append(bytes.data(), static_cast<int32_t>(bytes.size()));
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GraveyardBacktracking applied event={} flag={} label={} timing=pre_begin_play unfinished=preserved\n"),
                    event, flag, label);
            return true;
        };
        bool success = true;
        if (variant == 0)
        {
            struct Arena { wchar_t area; const wchar_t* flag; const wchar_t* label; int event1; int event2; };
            constexpr Arena arenas[]{
                    {L'A', L"gameflow_KG_13_A", L"KG_02_EVENT_A_END", 0, 3},
                    {L'B', L"gameflow_KG_13_B", L"KG_02_EVENT_B_END", 8, 12},
                    {L'C', L"gameflow_KG_13_C", L"KG_02_EVENT_C_END", 76, 81},
                    {L'D', L"gameflow_KG_13_D", L"KG_02_EVENT_D_END", 188, 180}};
            for (const auto& arena : arenas)
                for (int side = 1; side <= 2; ++side)
                {
                    const auto event = std::wstring(L"BndEvt__lv_TriggerBox_Area") + arena.area + L"_Start" +
                            std::to_wstring(side) + L"_K2Node_ActorBoundEvent_" +
                            std::to_wstring(side == 1 ? arena.event1 : arena.event2) +
                            L"_ActorBeginOverlapSignature__DelegateSignature";
                    success = patch(event, arena.flag, arena.label, false) && success;
                }
            // The mission-end remote events are an independent entrance to
            // the arena intro, even when the arena overlap itself is guarded.
            for (const wchar_t area : {L'A', L'B'})
                for (const auto* phase : {L"Start", L"Clear"})
                {
                    const auto event = std::wstring(L"kg_02_Mission_Area_") + area + L"_REvt_Mission_" + phase;
                    const auto flag = std::wstring(L"gameflow_KG_13_") + area;
                    const auto label = std::wstring(L"KG_02_EVENT_") + area + L"_END";
                    success = patch(event, flag.c_str(), label.c_str(), true) && success;
                }
        }
        else if (variant == 1)
        {
            success = patch(L"BndEvt__Trigger_AreaA_Mission_Start_K2Node_ActorBoundEvent_0_ActorBeginOverlapSignature__DelegateSignature",
                    L"gameflow_KG_13_A", L"KG_02_EVENT_A_START", true);
            for (const auto* event : {
                    L"BndEvt__Trigger_AreaA_Mission_End_K2Node_ActorBoundEvent_22_ActorBeginOverlapSignature__DelegateSignature",
                    L"BndEvt__Trigger_AreaA_Mission_RushEnd_K2Node_ActorBoundEvent_2_OnTresActorBeginOverlap__DelegateSignature",
                    L"kg_02_gameflow_REvt_Mission_Area_A_RetryStart"})
                success = patch(event, L"gameflow_KG_13_A", L"KG_02_EVENT_A_START", true) && success;
        }
        else
        {
            success = patch(L"BndEvt__kg_EventTrigger_MissionToB_K2Node_ActorBoundEvent_7_ActorBeginOverlapSignature__DelegateSignature",
                    L"gameflow_KG_13_B", L"KG_02_EVENT_B_START", true);
            // The rising walls have their own overlap/wall-run events. They
            // are not children of the mission-start execution branch.
            constexpr const wchar_t* events[]{
                    L"BndEvt__UpperTriggerBox_01_K2Node_ActorBoundEvent_0_OnTresActorBeginOverlap__DelegateSignature",
                    L"BndEvt__UpperTriggerBox_05_K2Node_ActorBoundEvent_2_OnTresActorBeginOverlap__DelegateSignature",
                    L"BndEvt__UpperTriggerBox_04_K2Node_ActorBoundEvent_3_OnTresActorBeginOverlap__DelegateSignature",
                    L"BndEvt__UpperTriggerBox_06_K2Node_ActorBoundEvent_4_OnTresActorBeginOverlap__DelegateSignature",
                    L"BndEvt__UpperTriggerBox_02_K2Node_ActorBoundEvent_5_OnTresActorBeginOverlap__DelegateSignature",
                    L"BndEvt__UpperTriggerBox_03_K2Node_ActorBoundEvent_6_OnTresActorBeginOverlap__DelegateSignature",
                    L"BndEvt__g_kg_Upperwall_06_K2Node_ActorBoundEvent_33_OnBeginWallRun__DelegateSignature",
                    L"BndEvt__g_kg_Upperwall_07_K2Node_ActorBoundEvent_42_OnBeginWallRun__DelegateSignature",
                    L"BndEvt__kg_EventTrigger_Mission_End_K2Node_ActorBoundEvent_8_ActorBeginOverlapSignature__DelegateSignature",
                    L"kg_02_gameflow_REvt_Mission_Area_B_RetryStart"};
            for (const auto* event : events)
                success = patch(event, L"gameflow_KG_13_B", L"KG_02_EVENT_B_START", true) && success;
        }
        return success;
    }
}
