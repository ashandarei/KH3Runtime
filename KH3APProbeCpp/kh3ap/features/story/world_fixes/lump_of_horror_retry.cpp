#include "lump_of_horror_retry.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <UnrealDef.hpp>

namespace kh3ap
{
    auto try_apply_lump_of_horror_retry(RC::Unreal::UObject* object) -> bool
    {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(object))
        {
            return false;
        }
        auto* graph = object->GetFunctionByNameInChain(STR("ExecuteUbergraph_mi_04_gameflow"));
        if (!is_uobject_valid_fast(graph) ||
            graph->GetPathName() != L"/Game/Levels/mi/mi_04/umap/mi_04_gameflow.mi_04_gameflow_C:ExecuteUbergraph_mi_04_gameflow")
        {
            return false;
        }
        auto* method = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, STR("/Script/TresGame.TresEnemyPawn_e_ex701:OnRetrySkip"));
        if (!is_uobject_valid_fast(method))
        {
            return false;
        }
        auto& script = graph->GetScript();
        const auto result = patch_lump_retry_skip(
            {script.GetData(), static_cast<size_t>(script.Num())},
            reinterpret_cast<uintptr_t>(method->GetOuterPrivate()), reinterpret_cast<uintptr_t>(method));
        if (result != LumpRetryPatch::already_applied)
        {
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] LumpOfHorrorRetry skip_callback={} graph_bytes={}\n"),
                result == LumpRetryPatch::applied ? STR("repaired") : STR("layout_rejected"), script.Num());
        }
        return result != LumpRetryPatch::rejected;
    }
}
