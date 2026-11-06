#pragma once
#include "kh3ap/core/gfx/gfx_reflection_models.h"
#include <functional>
#include <string_view>
namespace RC::Unreal
{
    class FProperty;
    class FArrayProperty;
    class UObject;
    class UFunction;
} // namespace RC::Unreal
namespace kh3ap::gfx_detail
{
    using RC::Unreal::FArrayProperty;
    using RC::Unreal::FProperty;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    using AsValueWriter = std::function<bool(FProperty*, void*)>;
    auto find_as_value_input_property(UFunction* function_object) -> FProperty*;

    auto find_return_property(UFunction* function_object) -> FProperty*;

    auto find_invoke_array_property(UFunction* function_object) -> FArrayProperty*;

    auto try_gfx_set_member_value_with_writer(const GfxProbeFunctionPaths& paths, UObject* object,
                                              std::wstring_view member_name,
                                              const AsValueWriter& writer) -> bool;

    auto try_gfx_get_member_as_value(const GfxProbeFunctionPaths& paths, UObject* object,
                                     std::wstring_view member_name,
                                     const std::function<void(void*, FProperty*)>& reader) -> bool;

    auto try_gfx_invoke_single_value(const GfxProbeFunctionPaths& paths, UObject* object,
                                     std::wstring_view function_name, const AsValueWriter& writer,
                                     UObject** return_object) -> bool;
} // namespace kh3ap::gfx_detail
