#include "kh3ap/features/story/world_fixes/olympus_intro_camera.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <UnrealDef.hpp>
#include <string>

namespace kh3ap
{
    auto try_apply_olympus_intro_camera(RC::Unreal::UObject* object) -> bool
    {
        if (!is_uobject_valid_fast(object)) return false;
        auto* entry = object->GetFunctionByNameInChain(STR("he_03_gameflow_Evt_ms010_start"));
        auto* graph = object->GetFunctionByNameInChain(STR("ExecuteUbergraph_he_03_camera_ms010"));
        std::wstring entry_path;
        std::wstring graph_path;
        const std::wstring prefix = L"Function /Game/Levels/he/he_03/umap/"
                                    L"he_03_camera_ms010.he_03_camera_ms010_C:";
        if (!is_uobject_valid_fast(entry) || !is_uobject_valid_fast(graph) ||
            !try_get_uobject_full_name_fast(entry, entry_path) ||
            !try_get_uobject_full_name_fast(graph, graph_path) ||
            entry_path != prefix + L"he_03_gameflow_Evt_ms010_start" ||
            graph_path != prefix + L"ExecuteUbergraph_he_03_camera_ms010")
        {
            return false;
        }
        const auto script_name = [](const wchar_t* value) -> OlympusIntroScriptName {
            const RC::Unreal::FName name(value, RC::Unreal::FNAME_Add);
            return {name.GetComparisonIndex(), name.GetDisplayIndex(), name.GetNumber()};
        };
        auto& entry_script = entry->GetScript();
        auto& graph_script = graph->GetScript();
        const auto result = patch_olympus_intro_camera(
                entry_script.GetData(), static_cast<size_t>(entry_script.Num()),
                graph_script.GetData(), static_cast<size_t>(graph_script.Num()),
                script_name(L"ExecuteUbergraph_he_03_camera_ms010"),
                script_name(L"he_03_ms010s_REvt_ms010s_End"));
        if (result != OlympusIntroCameraResult::already_applied)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusIntroCamera result={} entry_size={} graph_size={} "
                        "timing=pre_begin_play camera=normal completion=original_remote_event\n"),
                    result == OlympusIntroCameraResult::applied ? STR("applied") : STR("rejected"),
                    entry_script.Num(), graph_script.Num());
        }
        return result != OlympusIntroCameraResult::rejected;
    }
}
