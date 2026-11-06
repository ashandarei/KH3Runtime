#include "kh3ap/features/ui/map_overlay/gfx_bindings.h"
#include "kh3ap/features/ui/map_overlay/support.h"
#include "kh3ap/core/reflection/owned_function_cache.h"
#include <Unreal/UObject.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <tuple>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    namespace map_overlay_detail
    {
        namespace display_info_layout
        {
            // Verified ASDisplayInfo wire layout; retain the native padding.
            constexpr int32_t bytes = 44;
            constexpr int32_t position_offset = 0;
            constexpr int32_t scale_offset = 12;
            constexpr int32_t rotation_offset = 24;
            constexpr int32_t alpha_offset = 36;
            constexpr int32_t visible_offset = 40;
            constexpr int32_t visible_bytes = 1;
            constexpr int32_t vector_bytes = sizeof(std::array<float, 3>);
            constexpr int32_t float_bytes = sizeof(float);
            constexpr int32_t position_parameter_bytes = 2 * float_bytes;
        } // namespace display_info_layout
        auto gfx_display_info(UObject* object) -> std::optional<GfxDisplayInfo>
        {
            if (!is_uobject_valid_fast(object))
            {
                return {};
            }
            static OwnedFunctionCache function{L"GetDisplayInfo"};
            static FWeakObjectPtr bound_function, structure;
            static FBoolProperty* visible{};
            auto* fn = function.resolve(object);
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != display_info_layout::bytes)
            {
                return {};
            }
            if (bound_function.Get() != fn || !is_uobject_valid_fast(structure.Get()))
            {
                bound_function = {};
                structure = {};
                visible = nullptr;
                auto* value = find_function_param_by_name(fn, L"ReturnValue");
                if (!value || value->GetClass().GetName() != L"StructProperty" ||
                    value->GetElementSize() != display_info_layout::bytes ||
                    value->GetOffset_Internal() != 0)
                {
                    return {};
                }
                auto* type = static_cast<FStructProperty*>(value)->GetStruct();
                if (!is_uobject_valid_fast(type) || type->GetName() != L"ASDisplayInfo")
                {
                    return {};
                }
                for (const auto& [name, offset, size, kind] :
                     {std::tuple{L"Position", display_info_layout::position_offset,
                                 display_info_layout::vector_bytes, L"StructProperty"},
                      std::tuple{L"Scale", display_info_layout::scale_offset,
                                 display_info_layout::vector_bytes, L"StructProperty"},
                      std::tuple{L"Rotation", display_info_layout::rotation_offset,
                                 display_info_layout::float_bytes, L"FloatProperty"},
                      std::tuple{L"Alpha", display_info_layout::alpha_offset,
                                 display_info_layout::float_bytes, L"FloatProperty"},
                      std::tuple{L"Visible", display_info_layout::visible_offset,
                                 display_info_layout::visible_bytes, L"BoolProperty"}})
                {
                    auto* field = find_struct_field_by_name(type, name);
                    if (!field || field->GetOffset_Internal() != offset ||
                        field->GetElementSize() != size || field->GetClass().GetName() != kind)
                    {
                        return {};
                    }
                }
                visible = static_cast<FBoolProperty*>(find_struct_field_by_name(type, L"Visible"));
                structure = make_uobject_weak(type);
                bound_function = make_uobject_weak(fn);
            }
            alignas(std::max_align_t) std::array<std::byte, display_info_layout::bytes> params{};
            object->ProcessEvent(fn, params.data());
            GfxDisplayInfo info;
            std::memcpy(info.position.data(), params.data() + display_info_layout::position_offset,
                        sizeof(info.position));
            std::memcpy(info.scale.data(), params.data() + display_info_layout::scale_offset,
                        sizeof(info.scale));
            std::memcpy(&info.rotation, params.data() + display_info_layout::rotation_offset,
                        sizeof(info.rotation));
            std::memcpy(&info.alpha, params.data() + display_info_layout::alpha_offset,
                        sizeof(info.alpha));
            info.visible = visible->GetPropertyValueInContainer(params.data());
            for (float v : {info.position[0], info.position[1], info.scale[0], info.scale[1],
                            info.rotation, info.alpha})
            {
                if (!std::isfinite(v))
                {
                    return {};
                }
            }
            return info;
        }

        auto gfx_position(UObject* object, double x, double y) -> bool
        {
            if (!is_uobject_valid_fast(object) || !std::isfinite(x) || !std::isfinite(y))
            {
                return false;
            }
            static OwnedFunctionCache function{L"SetPosition"};
            static FWeakObjectPtr bound;
            auto* fn = function.resolve(object);
            if (!is_uobject_valid_fast(fn) ||
                fn->GetParmsSize() != display_info_layout::position_parameter_bytes)
            {
                return false;
            }
            if (bound.Get() != fn)
            {
                for (const auto& [name, offset] :
                     {std::pair{L"X", 0}, std::pair{L"Y", display_info_layout::float_bytes}})
                {
                    auto* p = find_function_param_by_name(fn, name);
                    if (!p || p->GetClass().GetName() != L"FloatProperty" ||
                        p->GetElementSize() != sizeof(float) || p->GetOffset_Internal() != offset)
                    {
                        return false;
                    }
                }
                bound = make_uobject_weak(fn);
            }
            std::array<float, 2> params{static_cast<float>(x), static_cast<float>(y)};
            object->ProcessEvent(fn, params.data());
            return true;
        }
    } // namespace map_overlay_detail
} // namespace kh3ap
