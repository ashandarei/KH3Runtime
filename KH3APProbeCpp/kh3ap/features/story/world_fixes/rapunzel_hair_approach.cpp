#include "kh3ap/features/story/world_fixes/rapunzel_hair_approach.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <UnrealDef.hpp>
#include <string>

namespace kh3ap
{
    auto try_apply_rapunzel_hair_approach(RC::Unreal::UObject* object) -> bool
    {
        if (!is_uobject_valid_fast(object)) return false;
        auto* function = object->GetFunctionByNameInChain(
                STR("ExecuteUbergraph_ra_01_gimmick_HairACT"));
        std::wstring name;
        if (!is_uobject_valid_fast(function) ||
            !try_get_uobject_full_name_fast(function, name) ||
            name != L"Function /Game/Levels/ra/ra_01/umap/ra_01_gimmick_HairACT."
                    L"ra_01_gimmick_HairACT_C:ExecuteUbergraph_ra_01_gimmick_HairACT")
            return false;
        auto* camera_property = object->GetPropertyByNameInChain(STR("LeadCameraOK"));
        if (!camera_property || !camera_property->IsA<RC::Unreal::FBoolProperty>())
            return false;
        const RC::Unreal::FName run_name(STR("BP_SetRestrictRun"), RC::Unreal::FNAME_Add);
        const RapunzelApproachScriptName script_name{
                run_name.GetComparisonIndex(), run_name.GetDisplayIndex(), run_name.GetNumber()};
        auto& script = function->GetScript();
        const auto result = patch_rapunzel_hair_approach(
                script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0,
                reinterpret_cast<uintptr_t>(camera_property), script_name);
        if (result != RapunzelHairApproachResult::already_applied)
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] RapunzelHairApproach result={} script_size={} timing=pre_begin_play camera=normal run=unrestricted swing=preserved\n"),
                    result == RapunzelHairApproachResult::applied ? STR("applied") : STR("rejected"),
                    script.Num());
        return result != RapunzelHairApproachResult::rejected;
    }
}
