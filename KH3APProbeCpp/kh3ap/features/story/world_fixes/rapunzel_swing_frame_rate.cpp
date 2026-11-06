#include "kh3ap/features/story/world_fixes/rapunzel_swing_frame_rate.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <UnrealDef.hpp>
#include <string>

namespace kh3ap
{
    auto try_apply_rapunzel_swing_frame_rate(RC::Unreal::UObject* object) -> bool
    {
        if (!is_uobject_valid_fast(object)) return false;
        auto* function = object->GetFunctionByNameInChain(
                STR("ExecuteUbergraph_ra_01_camera_ms070"));
        std::wstring name;
        if (!is_uobject_valid_fast(function) ||
            !try_get_uobject_full_name_fast(function, name) ||
            name != L"Function /Game/Levels/ra/ra_01/umap/ra_01_camera_ms070."
                    L"ra_01_camera_ms070_C:ExecuteUbergraph_ra_01_camera_ms070")
            return false;

        auto* fps_function = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UFunction*>(
                nullptr, nullptr,
                STR("/Script/TresGame.TresGameBlueprintLibrary:TresSetEventCutsceneMaxFPSMode"));
        if (!is_uobject_valid_fast(fps_function)) return false;
        auto& script = function->GetScript();
        const auto result = patch_rapunzel_swing_frame_rate(
                script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0,
                reinterpret_cast<uintptr_t>(fps_function));
        if (result != RapunzelSwingFrameRateResult::already_applied)
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] RapunzelSwingFrameRate result={} script_size={} timing=pre_begin_play cap=gameplay_setting\n"),
                    result == RapunzelSwingFrameRateResult::applied ? STR("applied") : STR("rejected"),
                    script.Num());
        return result != RapunzelSwingFrameRateResult::rejected;
    }
}
