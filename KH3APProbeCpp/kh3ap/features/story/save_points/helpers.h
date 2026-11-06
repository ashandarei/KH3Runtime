#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace RC::Unreal
{
    class UObject;
    class UFunction;
    class UClass;
    class FProperty;
} // namespace RC::Unreal
namespace kh3ap::save_point_native
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UClass;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;

    // Game-thread only. Returned pointers are borrowed for the immediate call;
    // the cache retains serial-checked weak handles across frames and maps.
    auto try_resolve_cached_function(std::wstring_view function_path) -> UFunction*;

    // The caller must validate object in the current frame before using this.
    auto try_read_object_property_trusted(UObject* object, std::wstring_view property_name)
        -> UObject*;

    auto try_read_object_property(UObject* object, std::wstring_view property_name) -> UObject*;

    auto describe_object_property(UObject* object, std::wstring_view property_name) -> std::wstring;

    auto try_resolve_object_path(std::wstring_view raw_path, std::wstring* lookup_path) -> UObject*;

    auto try_write_object_property(UObject* object, std::wstring_view property_name, UObject* value,
                                   std::wstring* reason) -> bool;

    auto format_scalar_property_value(UObject* object, std::wstring_view property_name)
        -> std::wstring;

    auto try_write_object_function_param(FProperty* property, void* params, UObject* value) -> bool;

    auto try_write_class_function_param(FProperty* property, void* params, UClass* value) -> bool;

    auto try_read_object_return_value(UFunction* function_object, void* params) -> UObject*;
} // namespace kh3ap::save_point_native
