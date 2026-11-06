#include "keyblade_style_controller.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/reflection/receiver_function_cache.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <format>
#include <utility>
#include <vector>

namespace kh3ap::keyblade_options
{
    using namespace RC::Unreal;
    auto fail(std::wstring* failure_details, std::wstring message) -> bool
    {
        if (failure_details)
        {
            *failure_details = std::move(message);
        }
        return false;
    }

    auto try_call_controller_bool(UObject* controller, std::wstring_view function_path,
                                  std::wstring_view parameter_name, bool value,
                                  std::wstring* failure_details) -> bool
    {
        const RuntimeHotPathProfileScope profile{L"keyblade_style:controller_bool"};
        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            return fail(failure_details, STR("Sora controller is unavailable"));
        }

        static ReceiverFunctionCache<> functions;
        UFunction* function_object = functions.resolve(controller, function_path);
        if (!function_object)
        {
            function_object =
                UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, function_path);
        }
        if (!function_object)
        {
            return fail(failure_details,
                        std::format(STR("controller function unavailable path={}"), function_path));
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        FProperty* parameter = find_function_param_by_name(function_object, parameter_name);
        if (!parameter)
        {
            // The Restrict pin is shown with that label in the Blueprint, but
            // this cooked build does not retain that name in the reflected
            // UFunction.  It is the sole input BoolProperty, so use the
            // reflected type as a safe fallback (and accept common generated
            // aliases when present).
            constexpr std::array<std::wstring_view, 5> aliases{
                STR("InRestrict"), STR("bRestrict"), STR("bEnable"), STR("InEnable"), STR("Value")};
            for (const std::wstring_view alias : aliases)
            {
                parameter = find_function_param_by_name(function_object, alias);
                if (parameter)
                {
                    break;
                }
            }
        }
        if (!parameter)
        {
            parameter = find_first_function_param_by_class(function_object, {STR("BoolProperty")});
        }

        if (!write_bool_function_param(parameter, params.data(), value))
        {
            return fail(failure_details,
                        std::format(STR("controller bool parameter unavailable name={} path={}"),
                                    parameter_name, function_path));
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            controller->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
        }
        return true;
    }

    auto try_call_controller_no_params(UObject* controller, std::wstring_view function_path,
                                       std::wstring* failure_details) -> bool
    {
        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            return fail(failure_details, STR("Sora controller is unavailable"));
        }

        static ReceiverFunctionCache<> functions;
        UFunction* function_object = functions.resolve(controller, function_path);
        if (!function_object)
        {
            function_object =
                UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, function_path);
        }
        if (!function_object)
        {
            return fail(
                failure_details,
                std::format(STR("controller notification unavailable path={}"), function_path));
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            controller->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
        }
        return true;
    }

} // namespace kh3ap::keyblade_options
