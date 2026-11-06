#pragma once
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <array>
#include <stdexcept>

namespace kh3ap::kh02preview_detail
{
    using namespace RC::Unreal;
    inline constexpr std::size_t reflected_parameter_limit_bytes = 1024;
    inline constexpr int preview_texture_size_pixels = 768;
    inline constexpr int maximum_preview_material_slots = 64;
    inline constexpr int always_spawn_collision_handling = 1;
    inline constexpr int show_only_primitive_render_mode = 2;
    inline constexpr int scene_color_depth_capture_source = 3;
    inline constexpr int orthographic_projection = 1;
    inline constexpr int movable_component_mobility = 2;
    inline constexpr int hit_test_invisible_visibility = 4;
    inline constexpr int preview_z_order = 3900;
    inline constexpr float orthographic_width_cm = 210.0f;
    inline constexpr float camera_distance_cm = 400.0f;
    inline constexpr float camera_height_cm = 85.0f;
    inline constexpr float half_turn_degrees = 180.0f;
    inline constexpr float model_facing_offset_degrees = 90.0f;

    struct PreviewLight
    {
        float intensity;
        float pitch_degrees;
        float yaw_offset_degrees;
    };

    inline constexpr std::array<PreviewLight, 2> preview_lights{{{3.0f, -25.0f, 160.0f}, {1.0f, -10.0f, 250.0f}}};

    struct Call : Kh02ReflectedCall
    {
        Call(UObject* owner, const wchar_t* method)
            : Kh02ReflectedCall(owner, method, "Aqua preview", reflected_parameter_limit_bytes)
        {
        }

        Call(const wchar_t* owner, const wchar_t* method)
            : Kh02ReflectedCall(owner, method, "Aqua preview", reflected_parameter_limit_bytes)
        {
        }
    };

    inline auto object_field(UObject* owner, const wchar_t* name) -> UObject*
    {
        return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, name) : nullptr;
    }

    inline void integer_field(UObject* owner, const wchar_t* name, int value)
    {
        if (!is_uobject_valid_fast(owner) ||
            !write_integral_function_param(owner->GetPropertyByNameInChain(name), owner, value))
        {
            throw std::runtime_error("Missing Aqua preview integer field");
        }
    }
} // namespace kh3ap::kh02preview_detail
