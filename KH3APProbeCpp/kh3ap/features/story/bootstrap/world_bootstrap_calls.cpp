#include "kh3ap/features/story/bootstrap/world_bootstrap_calls.h"
#include "kh3ap/features/story/bootstrap/bootstrap_operation_timer.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/owned_function_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <UnrealDef.hpp>
#include <cstddef>
#include <vector>

namespace kh3ap::bootstrap_native
{
    using namespace RC;
    using namespace RC::Unreal;
    auto find_function(std::wstring_view path) -> UFunction*
    {
        static OwnedFunctionCache loading{STR("TresUI_IsClosedLoadingScreen")};
        static OwnedFunctionCache shop{STR("BP_TresSetShopLevel")};
        static OwnedFunctionCache create{STR("Create")};
        const bool widget_library = path == create_widget_function_path;
        auto* library =
            find_loaded_runtime_asset(widget_library ? widget_blueprint_library_default_path
                                                     : tres_blueprint_library_default_path);
        if (widget_library)
        {
            return create.resolve(library);
        }
        if (path == loading_screen_closed_function_path)
        {
            return loading.resolve(library);
        }
        if (path == set_shop_level_function_path)
        {
            return shop.resolve(library);
        }
        return nullptr;
    }

    auto find_default_object(std::wstring_view path, UFunction* function) -> UObject*
    {
        UObject* object = find_loaded_runtime_asset(path);
        if (!is_uobject_valid_fast(object))
        {
            object = try_get_function_default_object(function);
        }
        return is_uobject_valid_fast(object) ? object : nullptr;
    }

    auto write_class_function_param(UFunction* function, void* params, std::wstring_view param_name,
                                    UClass* value) -> bool
    {
        FProperty* const property = find_function_param_by_name(function, param_name);
        if (!property || property->GetClass().GetName() != STR("ClassProperty"))
        {
            return false;
        }
        UClass** const value_ptr = property->ContainerPtrToValuePtr<UClass*>(params);
        if (!value_ptr)
        {
            return false;
        }
        *value_ptr = value;
        return true;
    }

    auto read_bool_function_param(UFunction* function, void* params, std::wstring_view param_name)
        -> std::optional<bool>
    {
        FProperty* const property = find_function_param_by_name(function, param_name);
        if (!property || property->GetClass().GetName() != STR("BoolProperty"))
        {
            return std::nullopt;
        }
        return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(params);
    }

    auto call_loading_screen_closed(std::wstring* failure) -> std::optional<bool>
    {
        UFunction* const function = find_function(loading_screen_closed_function_path);
        UObject* const self = find_default_object(tres_blueprint_library_default_path, function);
        if (!function || !self)
        {
            if (failure)
            {
                *failure = !function ? STR("missing_loading_screen_function")
                                     : STR("missing_tres_blueprint_library_default");
            }
            return std::nullopt;
        }

        const int params_size = function->GetParmsSize();
        if (params_size <= 0)
        {
            if (failure)
            {
                *failure = STR("loading_screen_function_has_no_return_storage");
            }
            return std::nullopt;
        }
        std::vector<std::byte> params(static_cast<size_t>(params_size));
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self->ProcessEvent(function, params.data());
        }
        const std::optional<bool> closed =
            read_bool_function_param(function, params.data(), STR("ReturnValue"));
        if (!closed.has_value() && failure)
        {
            *failure = STR("missing_loading_screen_return_value");
        }
        return closed;
    }

    auto call_set_shop_level(UObject* world_context, std::wstring* failure) -> bool
    {
        const BootstrapOperationTimer timing{STR("shop_level"), STR("shop")};
        UFunction* const function = find_function(set_shop_level_function_path);
        UObject* const self = find_default_object(tres_blueprint_library_default_path, function);
        if (!function || !self)
        {
            if (failure)
            {
                *failure = !function ? STR("missing_set_shop_level_function")
                                     : STR("missing_tres_blueprint_library_default");
            }
            return false;
        }

        const int params_size = function->GetParmsSize();
        if (params_size <= 0)
        {
            if (failure)
            {
                *failure = STR("set_shop_level_has_no_params");
            }
            return false;
        }
        std::vector<std::byte> params(static_cast<size_t>(params_size));
        FProperty* const level_param = find_function_param_by_name(function, STR("InShopLevel"));
        if (!write_object_function_param(function, params.data(), STR("WorldContextObject"),
                                         world_context) ||
            !write_integral_function_param(level_param, params.data(),
                                           static_cast<uint64_t>(runtime_goa_shop_level)))
        {
            if (failure)
            {
                *failure = STR("set_shop_level_param_write_failed");
            }
            return false;
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self->ProcessEvent(function, params.data());
        }
        return true;
    }

    auto create_and_add_proof_ui(UObject* world_context, UClass* widget_class,
                                 UObject** created_widget, std::wstring* failure) -> bool
    {
        UFunction* const create_function = find_function(create_widget_function_path);
        UObject* const create_self =
            find_default_object(widget_blueprint_library_default_path, create_function);
        if (!create_function || !create_self)
        {
            if (failure)
            {
                *failure = !create_function ? STR("missing_create_widget_function")
                                            : STR("missing_widget_blueprint_library_default");
            }
            return false;
        }
        const int create_params_size = create_function->GetParmsSize();
        if (create_params_size <= 0)
        {
            if (failure)
            {
                *failure = STR("create_widget_has_no_params");
            }
            return false;
        }
        std::vector<std::byte> create_params(static_cast<size_t>(create_params_size));
        // Let UMG resolve the first local player from this verified world
        // context. A controller exposed during travel can exist before its
        // Player is attached, which makes CreateWidget reject it.
        if (!write_object_function_param(create_function, create_params.data(),
                                         STR("WorldContextObject"), world_context) ||
            !write_class_function_param(create_function, create_params.data(), STR("WidgetType"),
                                        widget_class) ||
            !write_object_function_param(create_function, create_params.data(), STR("OwningPlayer"),
                                         nullptr))
        {
            if (failure)
            {
                *failure = STR("create_widget_param_write_failed");
            }
            return false;
        }
        {
            const BootstrapOperationTimer timing{STR("create_widget"), STR("proof_ui")};
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            create_self->ProcessEvent(create_function, create_params.data());
        }
        UObject* const widget = try_read_function_object_param(
            create_function, create_params.data(), STR("ReturnValue"));
        if (!is_uobject_valid_fast(widget))
        {
            if (failure)
            {
                *failure = STR("create_widget_returned_null");
            }
            return false;
        }

        static OwnedFunctionCache add_to_viewport{STR("AddToViewport")};
        UFunction* const add_function = add_to_viewport.resolve(widget);
        if (!add_function)
        {
            if (failure)
            {
                *failure = STR("missing_add_to_viewport_function");
            }
            return false;
        }
        const int add_params_size = add_function->GetParmsSize();
        if (add_params_size <= 0)
        {
            if (failure)
            {
                *failure = STR("add_to_viewport_has_no_params");
            }
            return false;
        }
        std::vector<std::byte> add_params(static_cast<size_t>(add_params_size));
        FProperty* const z_order = find_function_param_by_name(add_function, STR("ZOrder"));
        if (!write_integral_function_param(z_order, add_params.data(), 0))
        {
            if (failure)
            {
                *failure = STR("add_to_viewport_param_write_failed");
            }
            return false;
        }
        {
            const BootstrapOperationTimer timing{STR("add_to_viewport"), STR("proof_ui")};
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            widget->ProcessEvent(add_function, add_params.data());
        }
        if (created_widget)
        {
            *created_widget = widget;
        }
        return true;
    }

    auto report_failure(std::wstring_view operation, std::wstring& previous,
                        std::wstring_view failure) -> void
    {
        if (failure.empty() || previous == failure)
        {
            return;
        }
        previous = failure;
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] RuntimeWorldBootstrap operation={} status=deferred reason={}\n"),
            operation, failure);
    }
} // namespace kh3ap::bootstrap_native
