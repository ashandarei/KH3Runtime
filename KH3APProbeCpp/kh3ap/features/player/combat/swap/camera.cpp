#include "swap_impl.h"
#include "swap_state.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/movement/character_floating.h"
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/NumericPropertyTypes.hpp>
#include <cmath>
#include <algorithm>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/diagnostics/character_swap_trace.h"
#endif

namespace kh3ap::combat_swap_detail
{
    using namespace RC::Unreal;

    auto camera_manager(UObject* pawn) -> UObject*
    {
        return object(object(pawn, L"Controller"), L"PlayerCameraManager");
    }

    auto camera_orbit(UObject* pawn) -> CameraOrbit
    {
        auto* camera = object(camera_manager(pawn), L"m_CameraNormal1st");
        return {field<std::array<float, 3>>(camera, 0x898),
                field<std::array<float, 3>>(camera, 0x8a4),
                field<std::array<float, 3>>(camera, 0x8b0),
                field<std::array<float, 3>>(camera, 0x8c0),
                field<std::array<float, 3>>(camera, 0x8cc),
                vector(camera, L"GetCameraLocalOffset"),
                field<float>(camera, 0x8e4),
                field<float>(camera, 0x8e8),
                field<float>(camera, 0x8ec),
                static_cast<uint8_t>(field<uint8_t>(camera, 0x468) & 1u),
                static_cast<uint8_t>(field<uint8_t>(camera, 0x8f0) & 0x10u)};
    }

    auto camera_distance(UObject* pawn) -> CameraDistance
    {
        auto* camera = object(camera_manager(pawn), L"m_CameraNormal1st");
        if (!is_uobject_valid_fast(camera))
        {
            throw std::runtime_error("kh02_combat_swap_distance_camera");
        }
        for (const auto& [name, offset] :
             {std::pair{L"m_CurrentDistance", 0x428}, std::pair{L"m_Distance", 0x800},
              std::pair{L"m_NonBattleDistance", 0x900}})
        {
            auto* property = camera->GetPropertyByNameInChain(name);
            if (!property || property->GetOffset_Internal() != offset ||
                property->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("kh02_combat_swap_distance_layout");
            }
        }
        return {field<float>(camera, 0x428), field<float>(camera, 0x800),
                field<float>(camera, 0x900)};
    }

    auto camera_handoff_distance(UObject* pawn) -> CameraDistance
    {
        auto zoom = camera_distance(pawn);
        auto* camera = object(camera_manager(pawn), L"m_CameraNormal1st");
        Call configured_distance(camera, L"GetDefaultDistance");
        configured_distance.run();
        auto* result = configured_distance.param(L"ReturnValue");
        if (!result || !result->IsA<FFloatProperty>() || result->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("character_swap_camera_default_distance_layout");
        }
        // Dive-fall animates m_Distance down to 30. Its state owns the
        // eventual reset, which cannot follow a replacement pawn. Carry
        // the rendered distance for continuity, but converge to the source
        // camera's authored distance instead of retaining that state zoom.
        zoom.battle = *result->ContainerPtrToValuePtr<float>(configured_distance.parameters.data());
        if (!std::isfinite(zoom.battle) || zoom.battle <= 0.0f)
        {
            throw std::runtime_error("character_swap_camera_default_distance_invalid");
        }
        return zoom;
    }

    auto camera_timestamp(UObject* manager) -> float*
    {
        auto* cache = is_uobject_valid_fast(manager)
                          ? manager->GetPropertyByNameInChain(L"CameraCache")
                          : nullptr;
        auto* type = cache && cache->IsA<FStructProperty>()
                         ? static_cast<FStructProperty*>(cache)->GetStruct()
                         : nullptr;
        auto* timestamp = type ? find_struct_field_by_name(type, L"Timestamp") : nullptr;
        if (!timestamp || !timestamp->IsA<FFloatProperty>() ||
            timestamp->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("character_swap_camera_timestamp_layout");
        }
        return reinterpret_cast<float*>(cache->ContainerPtrToValuePtr<std::byte>(manager) +
                                        timestamp->GetOffset_Internal());
    }

    auto camera_pov(UObject* manager, const wchar_t* name) -> std::byte*
    {
        auto* entry =
            is_uobject_valid_fast(manager) ? manager->GetPropertyByNameInChain(name) : nullptr;
        if (!entry || !entry->IsA<FStructProperty>())
        {
            throw std::runtime_error("kh02_combat_swap_camera_cache");
        }
        auto* entry_type = static_cast<FStructProperty*>(entry)->GetStruct();
        auto* pov = find_struct_field_by_name(entry_type, L"POV");
        if (!pov || !pov->IsA<FStructProperty>())
        {
            throw std::runtime_error("kh02_combat_swap_camera_pov");
        }
        auto* view_type = static_cast<FStructProperty*>(pov)->GetStruct();
        auto* fov = find_struct_field_by_name(view_type, L"FOV");
        if (!view_type || view_type->GetName() != L"MinimalViewInfo" || !fov ||
            fov->GetOffset_Internal() != 24 || fov->GetElementSize() != sizeof(float) ||
            pov->GetElementSize() < sizeof(CameraView))
        {
            throw std::runtime_error("kh02_combat_swap_camera_view_layout");
        }
        return entry->ContainerPtrToValuePtr<std::byte>(manager) + pov->GetOffset_Internal();
    }

    void restore_camera(UObject* pawn, const std::array<float, 3>& control_rotation,
                        const CameraView& rendered_view, float rendered_timestamp,
                        const CameraDistance& zoom, const CameraOrbit& orbit)
    {
        auto* controller = object(pawn, L"Controller");
        auto* manager = camera_manager(pawn);
        auto* camera = object(manager, L"m_CameraNormal1st");
        auto* root = object(camera, L"RootComponent");
        auto* distance = is_uobject_valid_fast(camera)
                             ? camera->GetPropertyByNameInChain(L"m_CurrentDistance")
                             : nullptr;
        if (!is_uobject_valid_fast(controller) || object(controller, L"Pawn") != pawn ||
            !is_uobject_valid_fast(root) || !distance || distance->GetOffset_Internal() != 0x428)
        {
            throw std::runtime_error("kh02_combat_swap_camera_context");
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto camera_vtable = field<uintptr_t>(camera, 0);
        if (field<uintptr_t>(reinterpret_cast<void*>(camera_vtable), 0x7a0) !=
            base + character_native::rva(character_native::Site::camera_target_init))
        {
            throw std::runtime_error("kh02_combat_swap_camera_profile");
        }
        Call control(controller, L"SetControlRotation");
        control.vector(L"NewRotation", control_rotation[0], control_rotation[1],
                       control_rotation[2]);
        control.run();
        const auto& camera_rotation = rendered_view.rotation;
        Call view(root, L"K2_SetWorldLocationAndRotation");
        view.vector(L"NewLocation", rendered_view.location[0], rendered_view.location[1],
                    rendered_view.location[2]);
        view.vector(L"NewRotation", camera_rotation[0], camera_rotation[1], camera_rotation[2]);
        view.boolean(L"bSweep", false);
        view.boolean(L"bTeleport", true);
        view.run();
        // EGS profile checked by install(). Preserve the orbit's target and
        // rendered history separately, together with its scalar tracking
        // state. Target UObject ownership stays with the new camera.
        auto* bytes = reinterpret_cast<std::byte*>(camera);
        std::memcpy(bytes + 0x898, orbit.target_rotation.data(), sizeof(orbit.target_rotation));
        std::memcpy(bytes + 0x8a4, orbit.angular_delta.data(), sizeof(orbit.angular_delta));
        std::memcpy(bytes + 0x8b0, orbit.pivot.data(), sizeof(orbit.pivot));
        std::memcpy(bytes + 0x8c0, orbit.target_delta.data(), sizeof(orbit.target_delta));
        std::memcpy(bytes + 0x8cc, orbit.last_target.data(), sizeof(orbit.last_target));
        std::memcpy(bytes + 0x8e4, &orbit.recenter_wait, sizeof(float));
        std::memcpy(bytes + 0x8e8, &orbit.input_wait, sizeof(float));
        std::memcpy(bytes + 0x8ec, &orbit.reset_blend, sizeof(float));
        std::memcpy(bytes + 0x46c, camera_rotation.data(), sizeof(camera_rotation));
        auto* rotation_flags = reinterpret_cast<uint8_t*>(bytes + 0x468);
        *rotation_flags = static_cast<uint8_t>((*rotation_flags & ~1u) | orbit.reset_rotation);
        auto* orbit_flags = reinterpret_cast<uint8_t*>(bytes + 0x8f0);
        *orbit_flags = static_cast<uint8_t>((*orbit_flags & ~0x10u) | orbit.rotation_lerp);
        // Cancel a pooled camera's old state-owned distance animation via
        // its setter. A raw target write leaves that animation running.
        Call target_distance(camera, L"SetDistance");
        target_distance.number(L"Distance", zoom.battle);
        target_distance.number(L"Time", 0.0f);
        target_distance.run();
        // CameraCache is only the last rendered view. The camera component
        // also retains a state-owned FOV animation (120 during dive-fall).
        // Replace it through the setter so it cannot resume next frame.
        Call field_of_view(camera, L"SetFieldOfView");
        field_of_view.number(L"FieldOfView", rendered_view.fov);
        field_of_view.number(L"Time", 0.0f);
        field_of_view.run();
        // Dive-fall also animates an offset in camera space. Replace its
        // interpolation after leaving the cached state, or the stationary
        // incoming pawn's view still slides out of its old dive framing.
        Call local_offset(camera, L"BP_SetTresCameraLocalOffset");
        local_offset.vector(L"CameraLocalOffset", orbit.camera_local_offset[0],
                            orbit.camera_local_offset[1], orbit.camera_local_offset[2]);
        local_offset.number(L"Time", 0.0f);
        local_offset.run();
        // Preserve rendered continuity and the source camera's targets;
        // native orbit/collision smoothing handles the return from a dive.
        std::memcpy(bytes + 0x428, &zoom.current, sizeof(float));
        std::memcpy(bytes + 0x8bc, &zoom.current, sizeof(float));
        std::memcpy(bytes + 0x900, &zoom.normal, sizeof(float));
        // Possession has already populated these with the default view.
        // Rendering and view-target blending can consume them before the
        // field camera updates. Seed their scalar POV immediately, without
        // copying target pointers or post-process arrays.
        for (const auto* name :
             {L"CameraCache", L"LastFrameCameraCache", L"ViewTarget", L"PendingViewTarget"})
        {
            std::memcpy(camera_pov(manager, name), &rendered_view, sizeof(rendered_view));
        }
        // GetPlayerViewPoint ignores CameraCache while Timestamp is zero.
        // Carry the same-world source view's time too, so the first input
        // pass uses this view instead of the new controller's default yaw.
        *camera_timestamp(manager) = rendered_timestamp;
        character_floating::restore_camera_view(pawn);
    }
} // namespace kh3ap::combat_swap_detail

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace combat_swap_detail;

    void CharacterCombatSwap::Impl::update_camera(UObject* camera, float delta)
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const character_swap_trace::NativeScope trace{"camera_update"};
#endif
        auto* self = active;
        float applied_delta = delta;
        if (self->camera_transition_frames > 0 && self->transition_camera.Get() == camera &&
            actor_owning_world(camera) == self->request_world.Get())
        {
            // The camera must see the same first ground step as the pawn.
            // Doing this only in the later mod update leaves the camera
            // one step behind, then makes it catch up on the next frame.
            // Keep that later update as a fallback while control is unready.
            if (self->first_movement_pending)
            {
                try
                {
                    if (available(self->first_movement_pawn.Get()))
                    {
                        self->complete_first_movement(Clock::now());
                    }
                }
                catch (const std::exception& failure)
                {
                    self->error = failure.what();
                }
            }
            // Hero creation can inflate the first camera tick. Preserve
            // the established smoothing lead across that synchronous hitch.
            applied_delta = std::min(delta, self->camera_frame_delta);
            --self->camera_transition_frames;
            auto* bytes = reinterpret_cast<std::byte*>(camera);
            auto* reset_flags = reinterpret_cast<uint8_t*>(bytes + 0x468);
            auto* orbit_flags = reinterpret_cast<uint8_t*>(bytes + 0x8f0);
            [[maybe_unused]] const auto reset_before = *reset_flags;
            *reset_flags = static_cast<uint8_t>((*reset_flags & ~1u) | self->orbit.reset_rotation);
            *orbit_flags =
                static_cast<uint8_t>((*orbit_flags & ~0x10u) | self->orbit.rotation_lerp);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            self->camera_deltas.push_back(
                {{"native", delta},
                 {"applied", applied_delta},
                 {"reset_before", reset_before},
                 {"previous_rotation", field<std::array<float, 3>>(camera, 0x46c)}});
#endif
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const character_swap_trace::NativeScope original_trace{"camera_native_update"};
#endif
        reinterpret_cast<void (*)(UObject*, float)>(self->original_camera)(camera, applied_delta);
    }
} // namespace kh3ap
