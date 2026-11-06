#pragma once

#include "kh3ap/core/gfx/gfx_reflection_models.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    struct FFrame;
    struct FName;
    class FProperty;
    class FStructProperty;
    class UClass;
    class UObject;
    class UFunction;
    class UStruct;
    class UScriptStruct;
}

namespace kh3ap
{
    auto summarize_as_value_struct(
            void* value_ptr,
            RC::Unreal::FProperty* value_property) -> std::wstring;

    auto try_get_as_value_object(
            void* value_ptr,
            RC::Unreal::FProperty* value_property) -> RC::Unreal::UObject*;

    auto try_get_as_value_number(
            void* value_ptr,
            RC::Unreal::FProperty* value_property) -> std::optional<double>;

    auto try_get_as_value_string(
            void* value_ptr,
            RC::Unreal::FProperty* value_property) -> std::optional<std::wstring>;

    auto write_as_value_int(
            RC::Unreal::FProperty* value_property,
            void* value_ptr,
            int32_t value) -> bool;

    auto write_as_value_bool(
            RC::Unreal::FProperty* value_property,
            void* value_ptr,
            bool value) -> bool;

    auto write_as_value_number(
            RC::Unreal::FProperty* value_property,
            void* value_ptr,
            double value) -> bool;

    auto write_as_value_object(
            RC::Unreal::FProperty* value_property,
            void* value_ptr,
            RC::Unreal::UObject* value) -> bool;

    auto write_as_value_string(
            RC::Unreal::FProperty* value_property,
            void* value_ptr,
            std::wstring_view value) -> bool;

    auto try_gfx_invoke_get_child_at(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            int32_t index) -> RC::Unreal::UObject*;

    auto try_gfx_get_member_value(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name) -> std::wstring;

    auto try_gfx_get_member_value_object(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name) -> RC::Unreal::UObject*;

    auto try_gfx_get_member_value_number(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name) -> std::optional<double>;

    auto try_gfx_get_member_value_string(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name) -> std::optional<std::wstring>;

    auto try_gfx_set_member_value_string(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name,
            std::wstring_view value) -> bool;

    auto try_gfx_set_member_value_int(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name,
            int32_t value) -> bool;

    auto try_gfx_set_member_value_bool(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name,
            bool value) -> bool;

    auto try_gfx_set_member_value_number(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name,
            double value) -> bool;

    auto try_gfx_set_member_value_object(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name,
            RC::Unreal::UObject* value) -> bool;

    auto try_gfx_set_member_value_null(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view member_name) -> bool;

    auto try_gfx_invoke_no_args(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view function_name) -> bool;

    auto try_gfx_invoke_no_args_object(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view function_name) -> RC::Unreal::UObject*;

    auto try_gfx_invoke_string_arg_object(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view function_name,
            std::wstring_view value) -> RC::Unreal::UObject*;

    auto try_gfx_invoke_int_arg(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view function_name,
            int32_t value) -> bool;

    auto try_gfx_invoke_int_int_number_args(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view function_name,
            int32_t first,
            int32_t second,
            double third) -> bool;

    auto try_gfx_invoke_object_arg(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view function_name,
            RC::Unreal::UObject* value) -> bool;

    auto try_gfx_invoke_replace_text(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::wstring_view value) -> bool;

    auto summarize_gfx_member_values(
            const GfxProbeFunctionPaths& paths,
            std::wstring_view label,
            RC::Unreal::UObject* object) -> std::wstring;

    auto summarize_gfx_child_member_values(
            const GfxProbeFunctionPaths& paths,
            std::wstring_view label,
            RC::Unreal::UObject* object,
            int depth = 0) -> std::wstring;

    auto try_gfx_child_path(
            const GfxProbeFunctionPaths& paths,
            RC::Unreal::UObject* object,
            std::initializer_list<int32_t> path) -> RC::Unreal::UObject*;
}
