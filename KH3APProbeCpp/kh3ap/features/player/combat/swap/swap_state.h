#pragma once
#include "camera_state.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include <Unreal/NameTypes.hpp>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap::combat_swap_detail
{
    using RC::Unreal::FName;
    using RC::Unreal::UObject;
    using Clock = std::chrono::steady_clock;
    extern thread_local ReflectedPropertyCache<128> movement_properties;

    constexpr auto combo_grace = std::chrono::milliseconds(750);
    constexpr auto switch_timeout = std::chrono::seconds(2);
    constexpr float movement_intent_epsilon_squared = 0.0001f;
    constexpr float run_speed_epsilon_squared = 1.0f;
    constexpr float stick_continuity_epsilon_squared = 0.01f;
    constexpr size_t combo_index_offset = 0x128;
    constexpr size_t combo_finisher_offset = 0x140;
    constexpr size_t combo_air_offset = 0x142;
    extern const uintptr_t queue_clear_rva;
    extern const uintptr_t queue_push_rva;
    extern const uintptr_t queue_update_rva;

    struct Call : ReflectedFunctionCall
    {
        Call(UObject* owner, const wchar_t* method)
            : ReflectedFunctionCall(owner, method, "kh02_combat_swap", 256)
        {
        }

        Call(const wchar_t* owner, const wchar_t* method)
            : ReflectedFunctionCall(owner, method, "kh02_combat_swap", 256)
        {
        }
    };

    template <class T> auto field(void* object, size_t offset) -> T
    {
        T value{};
        SIZE_T copied{};
        if (!object ||
            !ReadProcessMemory(GetCurrentProcess(), static_cast<std::byte*>(object) + offset,
                               &value, sizeof(value), &copied) ||
            copied != sizeof(value))
        {
            throw std::runtime_error("kh02_combat_swap_native_field");
        }
        return value;
    }

    template <class T> auto property_value(UObject* owner, const wchar_t* name) -> T
    {
        auto* property = movement_properties.resolve(owner, name);
        if (!property || property->GetElementSize() != sizeof(T))
        {
            throw std::runtime_error("kh02_swap_movement_property");
        }
        return *property->ContainerPtrToValuePtr<T>(owner);
    }

    template <class T> void set_property(UObject* owner, const wchar_t* name, const T& value)
    {
        auto* property = movement_properties.resolve(owner, name);
        if (!property || property->GetElementSize() != sizeof(T))
        {
            throw std::runtime_error("kh02_swap_movement_property");
        }
        std::memcpy(property->ContainerPtrToValuePtr<void>(owner), &value, sizeof(value));
    }

    UObject* swap_context_world();
    int attack_kind(int player);
    float stick_distance_squared(const std::array<float, 2>& a, const std::array<float, 2>& b);
    auto object(UObject* owner, const wchar_t* name) -> UObject*;
    auto player_id(UObject* pawn) -> int;
    auto state(UObject* pawn) -> UObject*;
    auto state_id(UObject* current) -> int;
    bool attack_state(UObject* current, UObject* pawn);
    auto vector(UObject* owner, const wchar_t* method) -> std::array<float, 3>;
    bool state_boolean(UObject* owner, const wchar_t* name);
    auto animation_value(UObject* anim, const wchar_t* method) -> float;
    auto camera_manager(UObject* pawn) -> UObject*;
    auto local_player_pawn(UObject* world) -> UObject*;
    auto movement_stick(UObject* pawn) -> std::array<float, 2>;
    auto camera_orbit(UObject* pawn) -> CameraOrbit;
    auto camera_distance(UObject* pawn) -> CameraDistance;
    auto camera_handoff_distance(UObject* pawn) -> CameraDistance;
    auto camera_timestamp(UObject* manager) -> float*;
    auto camera_pov(UObject* manager, const wchar_t* name) -> std::byte*;
    void restore_camera(UObject* pawn, const std::array<float, 3>& control_rotation,
                        const CameraView& rendered_view, float rendered_timestamp,
                        const CameraDistance& zoom, const CameraOrbit& orbit);
} // namespace kh3ap::combat_swap_detail
