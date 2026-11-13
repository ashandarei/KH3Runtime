#pragma once
#include "kh3ap/features/ui/map_overlay/map_gfx_value_layout.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <UnrealDef.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <DynamicOutput/DynamicOutput.hpp>
#endif
namespace kh3ap::map_overlay_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    // Calls borrow synchronous strings and retain only serial-checked metadata.
    struct BorrowedString
    {
        const wchar_t* data;
        int32_t count, capacity;
    };
    inline constexpr size_t max_gfx_parameter_bytes = 256;
    inline constexpr size_t max_gfx_argument_bytes = 1024;
    // OpenMovie already returns the exact player. Retain only its weak identity,
    // not a live root/marker pointer or UE4SS's global path-search cache.
    struct MapMovieBinding
    {
        FWeakObjectPtr player;
        auto get() const -> UObject*
        {
            auto* value = player.Get();
            return is_uobject_valid_fast(value) ? value : nullptr;
        }
        auto remember(UObject* value) -> bool
        {
            player = make_uobject_weak(value);
            return value && get() == value;
        }
        auto reset() -> void
        {
            player = {};
        }
    };
    inline auto owned_function(UObject* owner, std::wstring_view path) -> UFunction*
    {
        if (!is_uobject_valid_fast(owner))
        {
            return nullptr;
        }
        auto* klass = owner->GetClassPrivate();
        if (!is_uclass_child_of_fast(klass, klass))
        {
            return nullptr;
        }
        const auto separator = path.rfind(L':');
        if (separator == std::wstring_view::npos || separator + 1 == path.size())
        {
            return nullptr;
        }
        const std::wstring name{path.substr(separator + 1)};
        auto* function = owner->GetFunctionByNameInChain(name.c_str());
        return is_uobject_a_fast(function, UFunction::StaticClass()) ? function : nullptr;
    }
    struct GfxValueBinding
    {
        FWeakObjectPtr structure;
        MapGfxValueLayout layout;
        FBoolProperty* boolean{};
        auto bind(FProperty* property) -> void
        {
            *this = {};
            if (!property || property->GetClass().GetName() != L"StructProperty")
            {
                return;
            }
            auto* type = static_cast<FStructProperty*>(property)->GetStruct();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (is_uobject_valid_fast(type))
            {
                auto* tag = find_struct_field_by_name(type, L"ASType");
                auto* flag = find_struct_field_by_name(type, L"ASBoolean");
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Area map value binding size={} structure={} typed={} "
                        "tag={} tag_size={} bool={} bool_size={}\n"),
                    property->GetElementSize(), type->GetPathName(),
                    is_uobject_a_fast(type, UScriptStruct::StaticClass()),
                    tag ? tag->GetClass().GetName() : L"none", tag ? tag->GetElementSize() : 0,
                    flag ? flag->GetClass().GetName() : L"none", flag ? flag->GetElementSize() : 0);
            }
#endif
            if (!is_uobject_a_fast(type, UScriptStruct::StaticClass()))
            {
                return;
            }
            layout.size = property->GetElementSize();
            if (layout.size <= 0 || layout.size > max_gfx_parameter_bytes)
            {
                layout = {};
                return;
            }
            auto field = [&](const wchar_t* name, const wchar_t* kind, int bytes) -> FProperty*
            {
                auto* value = find_struct_field_by_name(type, name);
                return value && value->GetClass().GetName() == kind &&
                               value->GetElementSize() == bytes &&
                               layout.contains(value->GetOffset_Internal(), bytes)
                           ? value
                           : nullptr;
            };
            auto* tag = field(L"ASType", L"ByteProperty", 1);
            if (!tag)
            {
                layout = {};
                return;
            }
            layout.type = tag->GetOffset_Internal();
            if (auto* value = field(L"ASInt", L"IntProperty", 4))
            {
                layout.integer = value->GetOffset_Internal();
            }
            if (auto* value = field(L"ASNumber", L"DoubleProperty", 8))
            {
                layout.number = value->GetOffset_Internal();
                layout.number_size = 8;
            }
            else if (auto* float_value = field(L"ASNumber", L"FloatProperty", 4))
            {
                layout.number = float_value->GetOffset_Internal();
                layout.number_size = 4;
            }
            if (auto* value = field(L"ASObject", L"ObjectProperty", sizeof(UObject*)))
            {
                layout.object = value->GetOffset_Internal();
            }
            boolean = static_cast<FBoolProperty*>(field(L"ASBoolean", L"BoolProperty", 1));
            structure = make_uobject_weak(type);
        }
        auto valid() const -> bool
        {
            return is_uobject_valid_fast(structure.Get());
        }
        auto number(void* value) const -> std::optional<double>
        {
            return valid() ? layout.read_number(value) : std::nullopt;
        }
        auto object(void* value) const -> UObject*
        {
            return valid() ? static_cast<UObject*>(layout.read_object(value)) : nullptr;
        }
        auto read_bool(void* value) const -> std::optional<double>
        {
            if (!valid() || !boolean || layout.read_type(value) != ASValueType::Boolean)
            {
                return {};
            }
            return boolean->GetPropertyValueInContainer(value) ? 1.0 : 0.0;
        }
        auto write_number(void* value, double input) const -> bool
        {
            return valid() && layout.write_number(value, input);
        }
        auto write_object(void* value, UObject* input) const -> bool
        {
            return valid() && layout.write_object(value, input);
        }
        auto write_bool(void* value, bool input) const -> bool
        {
            if (!valid() || !boolean || !value || !layout.contains(layout.type, 1))
            {
                return false;
            }
            std::memset(value, 0, layout.size);
            layout.write_type(value, ASValueType::Boolean);
            boolean->SetPropertyValueInContainer(value, input);
            return boolean->GetPropertyValueInContainer(value) == input;
        }
    };
    struct GfxBinding
    {
        std::wstring path;
        FWeakObjectPtr function;
        FProperty *name{}, *value{}, *result{};
        GfxValueBinding input_value, return_value;
        int name_offset{-1}, params_size{};
        auto borrow_name(void* params, const wchar_t* text) const -> bool
        {
            if (!params || !text || name_offset < 0)
            {
                return false;
            }
            const int32_t count = static_cast<int32_t>(std::wcslen(text) + 1);
            const BorrowedString input{text, count, count};
            std::memcpy(static_cast<std::byte*>(params) + name_offset, &input, sizeof(input));
            return true;
        }
        auto resolve(UObject* owner, std::wstring_view requested, bool invoke, bool setter = false)
            -> UFunction*
        {
            auto* fn = static_cast<UFunction*>(function.Get());
            if (path == requested && is_uobject_valid_fast(fn) && params_size > 0 &&
                (!result || return_value.valid()) && (!(setter || invoke) || input_value.valid()))
            {
                return fn;
            }
            path = requested;
            // The receiver already identifies the owning class. Avoid a global
            // UObject search when a binding is first used after loading a map.
            fn = owned_function(owner, path);
            function = make_uobject_weak(fn);
            name = value = result = nullptr;
            name_offset = -1;
            params_size = 0;
            input_value = {};
            return_value = {};
            if (!is_uobject_valid_fast(fn))
            {
                return nullptr;
            }
            const int size = fn->GetParmsSize();
            if (size <= 0 || size > max_gfx_parameter_bytes)
            {
                return nullptr;
            }
            auto bounded = [&](FProperty* property)
            {
                return property && property->GetOffset_Internal() >= 0 &&
                       property->GetElementSize() > 0 &&
                       property->GetOffset_Internal() <= size - property->GetElementSize();
            };
            name = find_function_param_by_name(fn, invoke ? L"FunctionName" : L"MemberName");
            value = find_function_param_by_name(fn, invoke ? L"Params" : L"Input");
            result = setter ? nullptr : find_function_param_by_name(fn, L"ReturnValue");
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Area map function binding path={} params={} name={} "
                    "name_size={} input={} return={}\n"),
                path, size, name ? name->GetClass().GetName() : L"none",
                name ? name->GetElementSize() : 0, value ? value->GetClass().GetName() : L"none",
                result ? result->GetClass().GetName() : L"none");
#endif
            if (!bounded(name) || name->GetClass().GetName() != L"StrProperty" ||
                name->GetElementSize() != sizeof(BorrowedString) ||
                ((setter || invoke) && !bounded(value)) || (!setter && !bounded(result)))
            {
                return nullptr;
            }
            name_offset = name->GetOffset_Internal();
            params_size = size;
            if (invoke && value->GetClass().GetName() == L"ArrayProperty")
            {
                input_value.bind(
                    const_cast<FProperty*>(static_cast<FArrayProperty*>(value)->GetInner()));
            }
            else
            {
                input_value.bind(value);
            }
            return_value.bind(result);
            return fn;
        }
    };
    // Only reflected metadata is retained. Dynamic UI values stay live.
} // namespace kh3ap::map_overlay_detail
