#include "kh3ap/features/story/world_fixes/olympus_collapse_camera.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <UnrealDef.hpp>
#include <mutex>
#include <string>

namespace kh3ap
{
    namespace
    {
        using namespace RC::Unreal;
        constexpr auto graph_path = L"Function /Game/Levels/he/he_03/umap/"
                L"he_03_camera_ms024s.he_03_camera_ms024s_C:ExecuteUbergraph_he_03_camera_ms024s";
        FName camera_class;
        FName begin_play;
        FName start_event;
        std::once_flag director_guard;

        bool disable_track(UObject* matinee, const wchar_t* class_path, const wchar_t* suffix)
        {
            auto* track_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, class_path);
            if (!is_uobject_valid_fast(track_class)) return false;
            const auto path = matinee->GetPathName() + suffix;
            auto* track = UObjectGlobals::StaticFindObject<UObject*>(track_class, nullptr, path.c_str());
            if (!is_uobject_valid_fast(track) || !track->IsA(track_class)) return false;
            auto* property = track->GetPropertyByNameInChain(L"bDisableTrack");
            if (!property || !property->IsA<FBoolProperty>()) return false;
            auto* flag = static_cast<FBoolProperty*>(property);
            const bool changed = !flag->GetPropertyValueInContainer(track);
            if (changed) flag->SetPropertyValueInContainer(track, true);
            return changed;
        }

        void before_collapse_event(UObject* object, UFunction* event, void*)
        {
            if (!event || (event->GetNamePrivate() != begin_play && event->GetNamePrivate() != start_event)) return;
            if (!is_uobject_valid_fast(object) || !is_uobject_valid_fast(object->GetClassPrivate()) ||
                object->GetClassPrivate()->GetNamePrivate() != camera_class) return;
            std::wstring class_path;
            if (!try_get_uobject_full_name_fast(object->GetClassPrivate(), class_path) ||
                class_path != L"BlueprintGeneratedClass /Game/Levels/he/he_03/umap/"
                              L"he_03_camera_ms024s.he_03_camera_ms024s_C") return;
            size_t changed = 0;
            size_t character_tracks = 0;
            for (const auto* property : {L"ms024s_01_ExecuteUbergraph_he_03_camera_ms024s_RefProperty",
                                        L"ms024s_02_ExecuteUbergraph_he_03_camera_ms024s_RefProperty"})
            {
                auto* matinee = try_read_reflected_object_property_value(object, property);
                if (!is_uobject_valid_fast(matinee) || matinee->GetOuterPrivate() != object->GetOuterPrivate()) continue;
                // Class-qualified lookup bypasses UE4SS's raw-pointer cache.
                // Resolve the current actor's track only, retaining no UObject.
                changed += disable_track(matinee, L"/Script/Engine.InterpTrackDirector",
                        L".InterpData_0.InterpGroupDirector_0.InterpTrackDirector_0");
                if (matinee->GetName() == L"ms024s_01")
                {
                    // Only this group's character binding is Sora (p_ex001).
                    // The second Matinee's group 0 animates rubble instead.
                    character_tracks += disable_track(matinee, L"/Script/Engine.InterpTrackMove",
                            L".InterpData_0.TresInterpGroup_0.InterpTrackMove_0");
                    character_tracks += disable_track(matinee, L"/Script/TresGame.TresInterpTrackFullBodyAnimation",
                            L".InterpData_0.TresInterpGroup_0.TresInterpTrackFullBodyAnimation_1");
                }
            }
            if (changed || character_tracks)
                RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] OlympusCollapseCamera directors_disabled={} character_tracks_disabled={} scenery=preserved\n"),
                        changed, character_tracks);
        }
    }

    auto try_apply_olympus_collapse_camera(RC::Unreal::UObject* object) -> bool
    {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(object)) return false;
        auto* graph = object->GetFunctionByNameInChain(L"ExecuteUbergraph_he_03_camera_ms024s");
        std::wstring path;
        if (!is_uobject_valid_fast(graph) || !try_get_uobject_full_name_fast(graph, path) || path != graph_path) return false;
        const auto name = [](const wchar_t* value) -> OlympusCollapseName {
            const FName n(value, FNAME_Add);
            return {n.GetComparisonIndex(), n.GetDisplayIndex(), n.GetNumber()};
        };
        const auto function = [](const wchar_t* value) -> uintptr_t {
            auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, value);
            return is_uobject_valid_fast(fn) ? reinterpret_cast<uintptr_t>(fn) : 0;
        };
        const OlympusCollapseCameraLinks links{
            function(L"/Script/TresGame.TresCameraTargetOTS:BP_SpawnTresCameraTargetOTS"),
            function(L"/Script/TresGame.TresGameBlueprintLibrary:EndAllSameBPTresCamera"),
            function(L"/Script/TresGame.TresGameBlueprintLibrary:EndAllBPTresCamera"),
            {name(L"exp01_cam"), name(L"EXP02_cam"), name(L"OTS")}, name(L"End Tres Camera Normal")};
        auto& script = graph->GetScript();
        const auto result = patch_olympus_collapse_camera(script.GetData(), script.Num(), links);
        const OlympusCollapseMovementLinks movement_links{
            function(L"/Script/TresGame.TresGameBlueprintLibrary:TresStartCinematicMode"),
            function(L"/Script/TresGame.TresGameBlueprintLibrary:TresEndCinematicMode"),
            function(L"/Script/TresGame.TresGameBlueprintLibrary:GetLocalTresPlayerPawnBase")};
        const auto movement = patch_olympus_collapse_movement(script.GetData(), script.Num(), movement_links);
        if (result != OlympusCollapseCameraResult::rejected)
        {
            // Use the existing ProcessEvent producer, after reference
            // serialization and before the first Matinee can play. No timer or
            // per-frame object search; BeginPlay also covers level reloads.
            std::call_once(director_guard, [] {
                camera_class = FName(runtime_olympus_collapse_camera_class, FNAME_Add);
                begin_play = FName(L"ReceiveBeginPlay", FNAME_Add);
                start_event = FName(L"he_03_gameflow_Evt_ms024s_01_start", FNAME_Add);
                Hook::RegisterProcessEventPreCallback(&before_collapse_event);
            });
        }
        if (result != OlympusCollapseCameraResult::already_applied)
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusCollapseCamera result={} script_size={} timing=pre_begin_play scenery=preserved\n"),
                    result == OlympusCollapseCameraResult::applied ? STR("applied") : STR("rejected"), script.Num());
        if (movement != OlympusCollapseCameraResult::already_applied)
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusCollapseMovement result={} script_size={} cinematic_lock=bypassed scenery=preserved\n"),
                    movement == OlympusCollapseCameraResult::applied ? STR("applied") : STR("rejected"), script.Num());
        return result != OlympusCollapseCameraResult::rejected && movement != OlympusCollapseCameraResult::rejected;
    }

    auto try_apply_olympus_collapse_completion(RC::Unreal::UObject* object) -> bool
    {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(object)) return false;
        auto* entry = object->GetFunctionByNameInChain(L"he_03_camera_ms024s_Evt_ms024s_01_end");
        auto* graph = object->GetFunctionByNameInChain(L"ExecuteUbergraph_he_03_gameflow");
        const std::wstring prefix = L"Function /Game/Levels/he/he_03/umap/he_03_gameflow.he_03_gameflow_C:";
        std::wstring entry_path;
        std::wstring owner_path;
        if (!is_uobject_valid_fast(entry) || !is_uobject_valid_fast(graph) ||
            !try_get_uobject_full_name_fast(entry, entry_path) ||
            !try_get_uobject_full_name_fast(graph, owner_path) ||
            entry_path != prefix + L"he_03_camera_ms024s_Evt_ms024s_01_end" ||
            owner_path != prefix + L"ExecuteUbergraph_he_03_gameflow") return false;
        const auto name = [](const wchar_t* value) -> OlympusCollapseName {
            const FName n(value, FNAME_Add);
            return {n.GetComparisonIndex(), n.GetDisplayIndex(), n.GetNumber()};
        };
        auto& event_script = entry->GetScript();
        auto& graph_script = graph->GetScript();
        const auto result = patch_olympus_collapse_completion(event_script.GetData(), event_script.Num(),
                graph_script.GetData(), graph_script.Num(), name(L"ExecuteUbergraph_he_03_gameflow"),
                name(L"he_03_FieldVoice_CollapseEvent_donald_0010"));
        if (result != OlympusCollapseCameraResult::already_applied)
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusCollapseCompletion result={} motion_reset=bypassed voice=preserved\n"),
                    result == OlympusCollapseCameraResult::applied ? STR("applied") : STR("rejected"));
        return result != OlympusCollapseCameraResult::rejected;
    }
}
