#include "kh3ap/features/story/world_fixes/big_hero_rescue_resume.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    auto try_apply_big_hero_rescue_resume(RC::Unreal::UObject* object) -> bool
    {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(object))
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(STR("ExecuteUbergraph_bx_02_gameflow"));
        if (!is_uobject_valid_fast(function) || function->GetPathName() !=
                L"/Game/Levels/bx/bx_02/umap/bx_02_gameflow.bx_02_gameflow_C:ExecuteUbergraph_bx_02_gameflow")
        {
            return false;
        }
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                STR("/Script/TresGame.Default__TresGameBlueprintLibrary"));
        auto* setter = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                STR("/Script/TresGame.TresGameBlueprintLibrary:SetGameFlagBoolean"));
        if (!is_uobject_valid_fast(library) || !is_uobject_valid_fast(setter))
        {
            return false;
        }
        std::array<HeroRescueScriptName, 4> flags{};
        constexpr const wchar_t* names[]{L"gameflow_BX_CE_SAVE_GOGO", L"gameflow_BX_CE_SAVE_WASABI",
                                        L"gameflow_BX_CE_SAVE_HONEY", L"gameflow_BX_CE_SAVE_FRED"};
        for (size_t index = 0; index < flags.size(); ++index)
        {
            const FName name(names[index], FNAME_Add);
            flags[index] = {name.GetComparisonIndex(), name.GetComparisonIndex(), name.GetNumber()};
        }
        auto& script = function->GetScript();
        const auto result = patch_big_hero_rescue_resume(
                {script.GetData(), static_cast<size_t>(script.Num())},
                reinterpret_cast<uintptr_t>(library), reinterpret_cast<uintptr_t>(setter), flags);
        if (result != HeroRescueResumeResult::AlreadyPatched)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BigHeroRescue resume patch applied={} size={} timing=pre_begin_play\n"),
                    result == HeroRescueResumeResult::Patched, script.Num());
        }
        return result != HeroRescueResumeResult::Rejected;
    }
}
