#include "kh3ap/features/grants/battle_level/battle_level_functions.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto RuntimeBattleLevelFunctions::resolve(bool getter) -> RuntimeBattleLevelFunction
    {
        auto* library = library_.Get();
        if (!is_uobject_valid_fast(library))
        {
            get_ = nullptr;
            set_ = nullptr;
            library = find_loaded_runtime_asset(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            auto* expected_class = find_loaded_runtime_asset_class(L"/Script/TresGame.TresGameBlueprintLibrary");
            if (!is_uobject_a_fast(library, expected_class)) return {};
            library_ = library;
        }
        auto* owner_class = library->GetClassPrivate();
        if (!is_uclass_child_of_fast(owner_class, owner_class)) return {};
        auto& cached_function = getter ? get_ : set_;
        auto* function = cached_function.Get();
        if (!is_uobject_a_fast(function, UFunction::StaticClass()) ||
            function->GetOuterPrivate() != owner_class)
        {
            function = library->GetFunctionByNameInChain(
                getter ? L"BP_TresGetBattleLevel" : L"BP_TresSetBattleLevel");
            if (!is_uobject_a_fast(function, UFunction::StaticClass()) ||
                function->GetOuterPrivate() != owner_class) return {};
            cached_function = function;
        }
        return {library, static_cast<UFunction*>(function)};
    }

    auto RuntimeBattleLevelFunctions::prepare() -> void
    {
        (void)resolve(true);
        (void)resolve(false);
    }
}
