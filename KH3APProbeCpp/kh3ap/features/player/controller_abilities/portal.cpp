#include "kh3ap/features/player/controller_abilities/host.h"

#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <cstring>
#include <string>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_portal_ability_outer_depth = 8;
    }

    using namespace RC;
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    auto RuntimeControllerAbilityService::Impl::portal_ownership_thunk(UObject*, FFrame& stack,
                                                                       void* result_storage) -> void
    {
        // P_FINISH for this zero-input native function: consume the caller's
        // EX_EndFunctionParms, or leave Code null for a ProcessEvent call.
        auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
        if (frame->Code)
        {
            ++frame->Code;
        }
        if (!result_storage)
        {
            return;
        }
        uint8_t result = 0;
        try
        {
            if (auto* instance = portal_instance_.load())
            {
                bool owned = false;
                std::wstring failure{};
                const bool available =
                    instance->try_read_unlock_owned(STR("attraction_flow"), &owned, &failure);
                result = available && owned ? 1 : 0;
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Portal Attraction Flow "
                                                    "ownership available={} owned={} failure={}\n"),
                                                available, owned, failure);
            }
        }
        catch (...)
        {
            // Failure must leave the enter-anyway warning usable.
            result = 0;
        }
        std::memcpy(result_storage, &result, sizeof(result));
    }

    auto RuntimeControllerAbilityService::Impl::ensure_portal_ownership_binding() -> bool
    {
        if (portal_function_.Get())
        {
            return true;
        }

        // A missing portal function makes StaticFindObject search the
        // object array on every sync. Only resolve it while Sora is in GoA;
        // a resident portal asset or an old context snapshot is not enough.
        auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.candidate_trusted() ? pawn_state.object() : nullptr;
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn))
        {
            return false;
        }
        bool in_goa = false;
        int depth = 0;
        for (UObject* outer = pawn->GetOuterPrivate();
             outer && kh3ap::is_uobject_valid_fast(outer) && depth < max_portal_ability_outer_depth;
             outer = outer->GetOuterPrivate(), ++depth)
        {
            if (outer->GetName() == STR("PersistentLevel"))
            {
                in_goa = outer->GetFullName().find(STR("/Game/Maps/GardenOfAssemblage/umap/")) !=
                         std::wstring::npos;
                break;
            }
        }
        if (!in_goa)
        {
            return true;
        }

        // A classless lookup uses UE4SS's raw global-object cache. After
        // GoA reloads, that address can belong to a different live UObject;
        // slot validity alone then cannot establish that it is a UFunction.
        // Supplying the class bypasses that cache and searches live functions.
        auto* const function_class = UFunction::StaticClass();
        if (!kh3ap::is_uobject_valid_fast(function_class))
        {
            return false;
        }
        // Resolve the resident class through the engine, then its owned
        // functions. No global UFunction scan or raw cached asset address
        // is needed after travel. The exact owning path still gates binding.
        auto* portal_class = find_loaded_runtime_asset_class(
            STR("/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/Portal_Base.Portal_Base_C"));
        if (!kh3ap::is_uobject_valid_fast(portal_class))
        {
            return false;
        }
        constexpr auto query_path = STR("/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/"
                                        "Portal_Base.Portal_Base_C:APHasAttractionFlow");
        UFunction* function = nullptr;
        for (auto* candidate : portal_class->ForEachFunctionInChain())
        {
            if (kh3ap::is_uobject_a_fast(candidate, function_class) &&
                candidate->GetName() == STR("APHasAttractionFlow") &&
                candidate->GetPathName() == query_path)
            {
                function = candidate;
                break;
            }
        }
        if (!kh3ap::is_uobject_a_fast(function, function_class) ||
            function->HasAnyFlags(static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad |
                                                            RF_NeedPostLoadSubobjects)))
        {
            return false;
        }
        // Some cooked exports have no weak serial in this UE4SS build.
        // Re-resolve those safely, but do not rebind/log an installed query.
        if (function->GetFuncPtr() == &Impl::portal_ownership_thunk &&
            (function->GetFunctionFlags() & FUNC_Native) != 0)
        {
            portal_instance_.store(this);
            portal_function_ = make_uobject_weak(function);
            return true;
        }
        auto* return_property = function->GetReturnProperty();
        if (function->GetNumParms() != 1 || function->GetParmsSize() != 1 ||
            function->GetReturnValueOffset() != 0 || !return_property ||
            !return_property->IsA<FBoolProperty>())
        {
            if (!portal_signature_warning_)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Portal ownership binding rejected unexpected signature; "
                        "warning remains available\n"));
            }
            portal_signature_warning_ = true;
            return true;
        }

        // Bind only our zero-input bool query as native. Steam does not
        // expose ProcessInternal to UE4SS, so RegisterHook on a Blueprint
        // function would throw and interrupt the rest of the periodic tick.
        portal_instance_.store(this);
        function->SetFuncPtr(&Impl::portal_ownership_thunk);
        function->GetFunctionFlags() |= FUNC_Native;
        portal_function_ = make_uobject_weak(function);
        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Portal Attraction Flow ability-list "
                                            "ownership native binding installed (116)\n"));
        return true;
    }
} // namespace kh3ap
