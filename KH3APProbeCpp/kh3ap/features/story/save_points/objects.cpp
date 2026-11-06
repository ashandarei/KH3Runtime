#include "kh3ap/features/story/save_points/internal.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace kh3ap::save_point_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using save_point_native::try_resolve_cached_function;

    namespace
    {
        UClass* retained_save_point_class{};
    }
    auto try_get_actor_location(UObject* pawn, std::wstring_view actor_location_function_path,
                                std::wstring* failure_reason) -> std::optional<SpawnVector>
    {
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn))
        {
            if (failure_reason)
            {
                *failure_reason = STR("invalid_pawn");
            }
            return std::nullopt;
        }

        UFunction* const function_object =
            try_resolve_cached_function(actor_location_function_path);
        if (!function_object)
        {
            if (failure_reason)
            {
                *failure_reason = STR("missing_actor_location_function");
            }
            return std::nullopt;
        }
        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!return_property)
        {
            if (failure_reason)
            {
                *failure_reason = STR("missing_actor_location_return_value");
            }
            return std::nullopt;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            pawn->ProcessEvent(function_object, params.data());
        }
        void* const value_ptr = return_property->ContainerPtrToValuePtr<void>(params.data());
        const int element_size = return_property->GetElementSize();
        if (!value_ptr || element_size < static_cast<int>(sizeof(float) * 3))
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("invalid_actor_location_return_size:{}"), element_size);
            }
            return std::nullopt;
        }

        if (element_size >= static_cast<int>(sizeof(double) * 3))
        {
            double values[3]{};
            std::memcpy(values, value_ptr, sizeof(values));
            return SpawnVector{values[0], values[1], values[2]};
        }

        float values[3]{};
        std::memcpy(values, value_ptr, sizeof(values));
        return SpawnVector{static_cast<double>(values[0]), static_cast<double>(values[1]),
                           static_cast<double>(values[2])};
    }

    [[nodiscard]] auto try_get_retained_save_point_class() -> UClass*
    {
        if (retained_save_point_class && kh3ap::is_uobject_valid_fast(retained_save_point_class))
        {
            return retained_save_point_class;
        }
        retained_save_point_class = nullptr;
        return nullptr;
    }

    auto retain_save_point_class(UClass* actor_class) -> bool
    {
        if (!actor_class || !kh3ap::is_uobject_valid_fast(actor_class))
        {
            return false;
        }
        if (UClass* const retained = try_get_retained_save_point_class())
        {
            return retained == actor_class;
        }

        // This generated class is a single bounded process-lifetime
        // dependency. Rooting it keeps the class, CDO, and component
        // templates alive across FZ02/HE02 package transitions. Do not
        // clear the root during DLL teardown, when GUObjectArray may
        // already be shutting down.
        actor_class->SetRootSet();
        retained_save_point_class = actor_class;
        return true;
    }

    auto try_resolve_save_point_class(const RuntimeSavePointSpawnDeps& deps,
                                      std::wstring* resolution_source) -> UClass*
    {
        if (UClass* const retained = try_get_retained_save_point_class())
        {
            if (resolution_source)
            {
                *resolution_source = STR("rooted_class_cache");
            }
            return retained;
        }

        UClass* const direct_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, std::wstring{deps.save_point_class_path}.c_str());
        if (direct_class && kh3ap::is_uobject_valid_fast(direct_class))
        {
            if (resolution_source)
            {
                *resolution_source = retain_save_point_class(direct_class)
                                         ? STR("class_path|class_rooted")
                                         : STR("class_path");
            }
            else
            {
                (void)retain_save_point_class(direct_class);
            }
            return direct_class;
        }

        UObject* const blueprint_object = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, std::wstring{deps.save_point_blueprint_path}.c_str());
        if (!blueprint_object || !kh3ap::is_uobject_valid_fast(blueprint_object))
        {
            if (resolution_source)
            {
                *resolution_source = STR("save_point_class_not_loaded");
            }
            return nullptr;
        }
        UClass** const generated_class =
            blueprint_object->GetValuePtrByPropertyName<UClass*>(STR("GeneratedClass"));
        if (generated_class && *generated_class && kh3ap::is_uobject_valid_fast(*generated_class))
        {
            if (resolution_source)
            {
                *resolution_source = retain_save_point_class(*generated_class)
                                         ? STR("blueprint_generated_class|class_rooted")
                                         : STR("blueprint_generated_class");
            }
            else
            {
                (void)retain_save_point_class(*generated_class);
            }
            return *generated_class;
        }
        if (resolution_source)
        {
            *resolution_source = STR("save_point_class_not_loaded");
        }
        return nullptr;
    }

    auto try_resolve_runtime_player_start_class(std::wstring* resolution_source) -> UClass*
    {
        constexpr std::array<std::wstring_view, 2> class_paths{
            STR("/Script/TresGame.TresPlayerStart"),
            STR("/Script/Engine.PlayerStart"),
        };
        for (std::wstring_view class_path : class_paths)
        {
            UClass* const actor_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, std::wstring{class_path}.c_str());
            if (actor_class && kh3ap::is_uobject_valid_fast(actor_class))
            {
                if (resolution_source)
                {
                    *resolution_source = std::wstring{class_path};
                }
                return actor_class;
            }
        }
        return nullptr;
    }

    auto try_get_gameplay_statics_default_object(std::wstring_view default_object_path,
                                                 UFunction* fallback_function) -> UObject*
    {
        UObject* self_object = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, std::wstring{default_object_path}.c_str());
        if (!self_object || !kh3ap::is_uobject_valid_fast(self_object))
        {
            self_object = try_get_function_default_object(fallback_function);
        }
        return self_object && kh3ap::is_uobject_valid_fast(self_object) ? self_object : nullptr;
    }

    auto try_read_bool_return_value(UFunction* function_object, void* params, bool fallback) -> bool
    {
        if (!function_object || !params)
        {
            return fallback;
        }
        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!return_property)
        {
            return fallback;
        }
        if (bool* const return_value = return_property->ContainerPtrToValuePtr<bool>(params))
        {
            return *return_value;
        }
        return fallback;
    }
} // namespace kh3ap::save_point_detail
