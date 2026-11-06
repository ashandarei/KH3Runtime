#include "kh3ap/features/grants/abilities/ability_query_helpers.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"

#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace kh3ap
{
    auto write_ability_kind_query_param(
            RC::Unreal::UFunction* function_object,
            void* params,
            uint64_t ability_value) -> bool
    {
        if (!function_object || !params)
        {
            return false;
        }

        const std::array<std::wstring_view, 4> candidate_names{
                STR("InAbilityKind"),
                STR("InAbility"),
                STR("AbilityKind"),
                STR("Ability"),
        };
        for (std::wstring_view name : candidate_names)
        {
            if (write_integral_function_param(find_function_param_by_name(function_object, name), params, ability_value))
            {
                return true;
            }
        }
        return false;
    }

    auto format_function_return_value(
            RC::Unreal::UFunction* function_object,
            void* params) -> std::wstring
    {
        if (!function_object || !params)
        {
            return STR("<none>");
        }
        RC::Unreal::FProperty* const return_property = find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!return_property)
        {
            return STR("<no_return>");
        }
        void* const value_ptr = return_property->ContainerPtrToValuePtr<void>(params);
        if (!value_ptr)
        {
            return STR("<return_unreadable>");
        }
        return format_hook_param_value(return_property, value_ptr, return_property->GetElementSize());
    }

}
