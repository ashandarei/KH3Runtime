#include "kh3ap/core/objects/asset_load_helpers.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/owned_function_cache.h"

#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FSoftObjectProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <cstddef>
#include <cstring>
#include <format>
#include <array>
#include <span>

namespace kh3ap
{
    namespace
    {
        constexpr auto asset_load_property_cache_capacity = 16;
        constexpr auto max_asset_load_parameter_bytes = 256;

        // Shipping FLatentActionInfo fields written into reflected parameter storage.
        namespace latent_info_layout
        {
            inline constexpr int minimum_bytes = 0x18;
            inline constexpr size_t uuid_offset = 0x4;
            inline constexpr size_t execution_function_offset = 0x8;
            inline constexpr size_t callback_target_offset = 0x10;
        } // namespace latent_info_layout

        thread_local std::wstring_view asset_completion_callback;
    } // namespace

    RuntimeAssetLoadCallbackScope::RuntimeAssetLoadCallbackScope(std::wstring_view callback)
        : previous_callback(asset_completion_callback)
    {
        asset_completion_callback = callback;
    }

    RuntimeAssetLoadCallbackScope::~RuntimeAssetLoadCallbackScope()
    {
        asset_completion_callback = previous_callback;
    }
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::wstring_view kismet_system_library_default_path =
            STR("/Script/Engine.Default__KismetSystemLibrary");
        constexpr std::wstring_view class_resolution_prefix = STR("kismet_load_asset_class");

        auto system_library() -> UObject*
        {
            static FWeakObjectPtr cached;
            if (auto* library = cached.Get(); is_uobject_valid_fast(library))
            {
                return library;
            }
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, kismet_system_library_default_path.data());
            cached = make_uobject_weak(library);
            return is_uobject_valid_fast(library) ? library : nullptr;
        }

        // These four engine calls have small fixed parameter records. Keep
        // scratch storage local so nested ProcessEvent calls cannot overwrite it.
        struct AssetParams
        {
            alignas(std::max_align_t) std::array<std::byte, max_asset_load_parameter_bytes> bytes{};
            auto data() -> void*
            {
                return bytes.data();
            }
            static auto supports(UFunction* function) -> bool
            {
                return is_uobject_valid_fast(function) && function->GetParmsSize() > 0 &&
                       function->GetParmsSize() <= max_asset_load_parameter_bytes;
            }
        };

        auto asset_parameter(UFunction* function, std::wstring_view name) -> FProperty*
        {
            struct Entry
            {
                FWeakObjectPtr owner;
                std::wstring name;
                FProperty* property{};
            };
            static std::array<Entry, asset_load_property_cache_capacity> entries;
            static size_t next{};
            for (const auto& entry : entries)
            {
                if (entry.owner.Get() == function && entry.name == name)
                {
                    return entry.property;
                }
            }
            auto* property = find_function_param_by_name(function, name);
            if (!property || !property->HasAnyPropertyFlags(CPF_Parm) ||
                property->GetOffset_Internal() < 0 || property->GetElementSize() <= 0 ||
                property->GetOffset_Internal() >
                    function->GetParmsSize() - property->GetElementSize())
            {
                return nullptr;
            }
            entries[next++ % entries.size()] = {make_uobject_weak(function), std::wstring{name},
                                                property};
            return property;
        }

        auto write_context(UFunction* function, void* params, UObject* context) -> bool
        {
            auto* property = asset_parameter(function, STR("WorldContextObject"));
            if (!property || property->GetClass().GetName() != STR("ObjectProperty") ||
                property->GetElementSize() != sizeof(context))
            {
                return false;
            }
            std::memcpy(property->ContainerPtrToValuePtr<void>(params), &context, sizeof(context));
            return true;
        }

        struct InitializedParamValue
        {
            FProperty* property{};
            void* value_ptr{};
        };

        auto set_resolution(std::wstring* resolution_source, std::wstring_view suffix) -> void
        {
            if (resolution_source)
            {
                *resolution_source = std::wstring{class_resolution_prefix};
                resolution_source->append(suffix);
            }
        }

        auto
        destroy_initialized_param_values(std::span<const InitializedParamValue> initialized_values)
            -> void
        {
            for (const InitializedParamValue& value : initialized_values)
            {
                if (value.property && value.value_ptr)
                {
                    destroy_property_value_dynamic(value.property, value.value_ptr);
                }
            }
        }

        auto write_soft_reference_param(
            UFunction* function_object, void* params, std::wstring_view parameter_name,
            std::wstring_view legacy_property_class, std::wstring_view soft_property_class,
            std::wstring_view asset_path, std::wstring_view diagnostic_prefix,
            InitializedParamValue* initialized_value, std::wstring* resolution_source) -> bool
        {
            FProperty* const property = asset_parameter(function_object, parameter_name);
            if (!property)
            {
                if (resolution_source)
                {
                    *resolution_source = std::format(STR("{}_missing_soft_param_{}"),
                                                     diagnostic_prefix, parameter_name);
                }
                return false;
            }

            const std::wstring property_class = property->GetClass().GetName();
            const bool legacy_property = property_class == legacy_property_class;
            const bool modern_property = property_class == soft_property_class;
            if (!legacy_property && !modern_property)
            {
                if (resolution_source)
                {
                    *resolution_source = std::format(STR("{}_unexpected_soft_param_{}"),
                                                     diagnostic_prefix, property_class);
                }
                return false;
            }

            void* const value_ptr = property->ContainerPtrToValuePtr<void>(params);
            const int element_size = property->GetElementSize();
            if (!value_ptr || element_size <= 0)
            {
                if (resolution_source)
                {
                    *resolution_source =
                        std::format(STR("{}_bad_soft_param_{}"), diagnostic_prefix, parameter_name);
                }
                return false;
            }

            constexpr size_t legacy_asset_reference_offset = 0x10;
            const bool storage_too_small =
                (legacy_property && legacy_asset_reference_offset + sizeof(FString) >
                                        static_cast<size_t>(element_size)) ||
                (modern_property && element_size < static_cast<int>(sizeof(FSoftObjectPtr)));
            if (storage_too_small)
            {
                if (resolution_source)
                {
                    *resolution_source =
                        std::format(STR("{}_soft_param_too_small_{}_{}"), diagnostic_prefix,
                                    parameter_name, element_size);
                }
                return false;
            }
            if (!initialize_property_value_dynamic(property, value_ptr))
            {
                if (resolution_source)
                {
                    *resolution_source =
                        std::format(STR("{}_soft_param_init_failed"), diagnostic_prefix);
                }
                return false;
            }
            *initialized_value = InitializedParamValue{
                .property = property,
                .value_ptr = value_ptr,
            };

            const std::wstring owned_path{asset_path};
            if (legacy_property)
            {
                auto* const legacy_asset_path = reinterpret_cast<FString*>(
                    static_cast<std::byte*>(value_ptr) + legacy_asset_reference_offset);
                *legacy_asset_path = FString(owned_path.c_str());
                return true;
            }

            auto* const soft_reference = static_cast<FSoftObjectPtr*>(value_ptr);
            soft_reference->ObjectID.AssetPathName = FName(owned_path, FNAME_Add);
            return true;
        }

        auto write_latent_info_param(UFunction* function_object, void* params,
                                     UObject* callback_target, int32_t uuid,
                                     std::wstring_view diagnostic_prefix,
                                     std::wstring* resolution_source) -> bool
        {
            FProperty* const property = asset_parameter(function_object, STR("LatentInfo"));
            if (!property || property->GetClass().GetName() != STR("StructProperty"))
            {
                if (resolution_source)
                {
                    *resolution_source =
                        std::format(STR("{}_missing_latent_info"), diagnostic_prefix);
                }
                return false;
            }

            void* const value_ptr = property->ContainerPtrToValuePtr<void>(params);
            if (!value_ptr || property->GetElementSize() < latent_info_layout::minimum_bytes)
            {
                if (resolution_source)
                {
                    *resolution_source = std::format(STR("{}_bad_latent_info"), diagnostic_prefix);
                }
                return false;
            }

            auto* const bytes = static_cast<std::byte*>(value_ptr);
            const int32_t linkage = 0;
            std::memcpy(bytes, &linkage, sizeof(linkage));
            std::memcpy(bytes + latent_info_layout::uuid_offset, &uuid, sizeof(uuid));
            if (!asset_completion_callback.empty())
            {
                const FName callback_name(asset_completion_callback, FNAME_Add);
                static_assert(sizeof(callback_name) == sizeof(uint64_t));
                std::memcpy(bytes + latent_info_layout::execution_function_offset, &callback_name,
                            sizeof(callback_name));
            }
            std::memcpy(bytes + latent_info_layout::callback_target_offset, &callback_target,
                        sizeof(callback_target));
            return true;
        }

        auto resolve_loaded_soft_reference(std::wstring_view path, bool class_reference) -> UObject*
        {
            if (path.empty())
            {
                return nullptr;
            }
            // This native CDO is already used by the latent request helpers.
            // Resolve the function on its owner, not with a global UFunction
            // search for each previously unseen conversion/party asset.
            UObject* const library = system_library();
            if (!library || !is_uobject_valid_fast(library))
            {
                return nullptr;
            }
            static OwnedFunctionCache class_conversion{STR("Conv_SoftClassReferenceToClass")};
            static OwnedFunctionCache object_conversion{STR("Conv_SoftObjectReferenceToObject")};
            UFunction* const function =
                (class_reference ? class_conversion : object_conversion).resolve(library);
            if (!function || !is_uobject_valid_fast(function) || !function->GetFuncPtr() ||
                !AssetParams::supports(function))
            {
                return nullptr;
            }
            FProperty* const returned = asset_parameter(function, STR("ReturnValue"));
            if (!returned || !returned->HasAnyPropertyFlags(CPF_ReturnParm) ||
                returned->GetClass().GetName() !=
                    (class_reference ? STR("ClassProperty") : STR("ObjectProperty")) ||
                returned->GetElementSize() != sizeof(UObject*))
            {
                return nullptr;
            }
            AssetParams params;
            std::array<InitializedParamValue, 1> initialized{};
            if (!write_soft_reference_param(
                    function, params.data(), class_reference ? STR("AssetClass") : STR("Asset"),
                    class_reference ? STR("AssetClassProperty") : STR("AssetObjectProperty"),
                    class_reference ? STR("SoftClassProperty") : STR("SoftObjectProperty"), path,
                    STR("resolve_loaded_asset"), initialized.data(), nullptr))
            {
                destroy_initialized_param_values(initialized);
                return nullptr;
            }
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                library->ProcessEvent(function, params.data());
            }
            UObject* object{};
            std::memcpy(&object, returned->ContainerPtrToValuePtr<void>(params.data()),
                        sizeof(object));
            destroy_initialized_param_values(initialized);
            constexpr auto pending_flags = static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad |
                                                                     RF_NeedPostLoadSubobjects);
            return object && is_uobject_valid_fast(object) && !object->HasAnyFlags(pending_flags)
                       ? object
                       : nullptr;
        }
    } // namespace

    auto find_loaded_runtime_asset(std::wstring_view asset_path) -> UObject*
    {
        return resolve_loaded_soft_reference(asset_path, false);
    }

    auto find_loaded_runtime_asset_class(std::wstring_view class_path) -> UClass*
    {
        auto* object = resolve_loaded_soft_reference(class_path, true);
        return is_uobject_a_fast(object, UClass::StaticClass()) ? static_cast<UClass*>(object)
                                                                : nullptr;
    }

    auto request_runtime_asset_load(UObject* world_context_object, std::wstring_view asset_path,
                                    std::wstring* resolution_source, int32_t latent_uuid) -> bool
    {
        if (!world_context_object || !is_uobject_valid_fast(world_context_object))
        {
            if (resolution_source)
            {
                *resolution_source = STR("kismet_load_asset_invalid_world_context");
            }
            return false;
        }

        UObject* const self_object = system_library();
        static OwnedFunctionCache load{STR("LoadAsset")};
        UFunction* const function_object = load.resolve(self_object);
        if (!function_object || !is_uobject_valid_fast(function_object))
        {
            if (resolution_source)
            {
                *resolution_source = STR("kismet_load_asset_missing_function");
            }
            return false;
        }
        if (!AssetParams::supports(function_object))
        {
            if (resolution_source)
            {
                *resolution_source = STR("kismet_load_asset_empty_params");
            }
            return false;
        }

        if (!is_uobject_valid_fast(self_object))
        {
            if (resolution_source)
            {
                *resolution_source = STR("kismet_load_asset_missing_self");
            }
            return false;
        }

        AssetParams params;
        std::array<InitializedParamValue, 1> initialized_values{};
        if (!write_context(function_object, params.data(), world_context_object) ||
            !write_soft_reference_param(function_object, params.data(), STR("Asset"),
                                        STR("AssetObjectProperty"), STR("SoftObjectProperty"),
                                        asset_path, STR("kismet_load_asset"),
                                        initialized_values.data(), resolution_source) ||
            !write_latent_info_param(function_object, params.data(), world_context_object,
                                     latent_uuid, STR("kismet_load_asset"), resolution_source))
        {
            if (resolution_source && resolution_source->empty())
            {
                *resolution_source = STR("kismet_load_asset_missing_world_context_param");
            }
            destroy_initialized_param_values(initialized_values);
            return false;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        destroy_initialized_param_values(initialized_values);
        if (resolution_source)
        {
            *resolution_source = STR("kismet_load_asset_requested");
        }
        return true;
    }

    auto request_runtime_asset_class_load(UObject* world_context_object,
                                          std::wstring_view class_path,
                                          std::wstring* resolution_source, int32_t latent_uuid)
        -> bool
    {
        if (!world_context_object || !is_uobject_valid_fast(world_context_object))
        {
            set_resolution(resolution_source, STR("_invalid_world_context"));
            return false;
        }

        UObject* const self_object = system_library();
        static OwnedFunctionCache load_class{STR("LoadAssetClass")};
        UFunction* const function_object = load_class.resolve(self_object);
        if (!function_object || !is_uobject_valid_fast(function_object))
        {
            set_resolution(resolution_source, STR("_missing_function"));
            return false;
        }
        if (!AssetParams::supports(function_object))
        {
            set_resolution(resolution_source, STR("_empty_params"));
            return false;
        }

        if (!is_uobject_valid_fast(self_object))
        {
            set_resolution(resolution_source, STR("_missing_self"));
            return false;
        }

        AssetParams params;
        std::array<InitializedParamValue, 1> initialized_values{};
        if (!write_context(function_object, params.data(), world_context_object) ||
            !write_soft_reference_param(function_object, params.data(), STR("AssetClass"),
                                        STR("AssetClassProperty"), STR("SoftClassProperty"),
                                        class_path, class_resolution_prefix,
                                        initialized_values.data(), resolution_source) ||
            !write_latent_info_param(function_object, params.data(), world_context_object,
                                     latent_uuid, class_resolution_prefix, resolution_source))
        {
            if (resolution_source && resolution_source->empty())
            {
                set_resolution(resolution_source, STR("_missing_world_context_param"));
            }
            destroy_initialized_param_values(initialized_values);
            return false;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        destroy_initialized_param_values(initialized_values);
        set_resolution(resolution_source, STR("_requested"));
        return true;
    }
} // namespace kh3ap
