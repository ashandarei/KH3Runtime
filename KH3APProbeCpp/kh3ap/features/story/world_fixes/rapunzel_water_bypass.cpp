#include "kh3ap/features/story/world_fixes/rapunzel_water_bypass.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <UnrealDef.hpp>
#include <string>

namespace kh3ap
{
    auto try_apply_rapunzel_water_bypass(RC::Unreal::UObject* object) -> bool
    {
        if (!is_uobject_valid_fast(object)) return false;
        auto* function = object->GetFunctionByNameInChain(
            STR("ExecuteUbergraph_ra_01_mission_Kando_Hill"));
        std::wstring name;
        if (!is_uobject_valid_fast(function) ||
            !try_get_uobject_full_name_fast(function, name) ||
            name != L"Function /Game/Levels/ra/ra_01/umap/ra_01_mission_Kando_Hill."
                    L"ra_01_mission_Kando_Hill_C:ExecuteUbergraph_ra_01_mission_Kando_Hill")
            return false;
        auto& script = function->GetScript();
        const auto result = patch_rapunzel_water_bytecode(
            script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0);
        if (result != RapunzelWaterPatchResult::already_applied)
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] RapunzelWaterBypass result={} script_size={} timing=pre_begin_play\n"),
                result == RapunzelWaterPatchResult::applied ? STR("applied") : STR("rejected"),
                script.Num());
        return result != RapunzelWaterPatchResult::rejected;
    }
}
