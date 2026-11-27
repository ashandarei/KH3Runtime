#include "swap_impl.h"
#include "swap_state.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/characters/character_exploration.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_idle.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <cmath>
#include <algorithm>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/diagnostics/character_swap_trace.h"
#endif

namespace kh3ap::combat_swap_detail
{
    using namespace RC::Unreal;

    float stick_distance_squared(const std::array<float, 2>& a, const std::array<float, 2>& b)
    {
        const float x = a[0] - b[0];
        const float y = a[1] - b[1];
        return x * x + y * y;
    }

    static bool has_motion(UObject* instance, FName name)
    {
        auto* sets = is_uobject_valid_fast(instance)
                         ? instance->GetPropertyByNameInChain(L"MyAnimSets")
                         : nullptr;
        if (!sets || !sets->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty*>(sets)->GetInner()->GetElementSize() != sizeof(UObject*))
        {
            return false;
        }
        using player_animation_support::AnimationEntry;
        for (auto* set : *sets->ContainerPtrToValuePtr<TArray<UObject*>>(instance))
        {
            auto* entries =
                is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
            if (!entries || !entries->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(entries)->GetInner()->GetElementSize() !=
                    sizeof(AnimationEntry))
            {
                continue;
            }
            for (const auto& entry : *entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(set))
            {
                if (entry.name == name && is_uobject_valid_fast(entry.asset))
                {
                    return true;
                }
            }
        }
        return false;
    }

    static FName resolve_run_motion(UObject* instance, FName source)
    {
        if (has_motion(instance, source))
        {
            return source;
        }
        // Some heroes author only the battle (B) locomotion set, whereas
        // exploration Sora uses normal (N). Keep the native startup/loop
        // motion instead of asking that hero to play a missing animation.
        auto name = source.ToString();
        if (!name.empty() && (name.back() == L'N' || name.back() == L'B'))
        {
            name.back() = name.back() == L'N' ? L'B' : L'N';
            const FName alternate{name.c_str()};
            if (has_motion(instance, alternate))
            {
                return alternate;
            }
        }
        throw std::runtime_error("character_run_motion_missing");
    }
} // namespace kh3ap::combat_swap_detail

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace combat_swap_detail;

    constexpr int run_loop_phase = 0;
    constexpr int run_entry_phase = 2;
    constexpr int run_stance_blend_phase = 3;
    constexpr size_t run_walk_active_offset = 0x94;
    constexpr size_t run_walk_input_offset = 0x95;
    constexpr size_t run_walk_grace_offset = 0x98;
    constexpr uint8_t walking_movement_mode = 1;
    constexpr float maximum_first_movement_delta_seconds = 0.1f;
    const uintptr_t air_jump_available_rva =
        character_native::rva(character_native::Site::air_jump_available);

    constexpr auto first_movement_grace = std::chrono::milliseconds(250);
    const uintptr_t set_air_jump_available_rva =
        character_native::rva(character_native::Site::set_air_jump_available);

    void CharacterCombatSwap::Impl::restore_movement_mode(UObject* pawn, UObject* movement)
    {
        if (!kh02_current_story_world())
        {
            // Only the incoming owned pawn is changed, during the existing
            // possession transaction. Ordinary swaps also clear stale bits.
            auto& floating_flags =
                *reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(pawn) + 0xd80);
            floating_flags = (floating_flags & ~3u) | floating_mode;
            if (floating_mode)
            {
                auto* component = object(pawn, L"MyStateComp");
                const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                reinterpret_cast<void (*)(UObject*)>(base + queue_clear_rva)(component);
                floating_flags = (floating_flags & ~3u) | floating_mode;
                auto* floating = reinterpret_cast<UObject* (*)(UObject*, int)>(
                    base + queue_push_rva)(component, player_state_ids::floating);
                if (!is_uobject_valid_fast(floating) || object(floating, L"MyCharPawn") != pawn)
                {
                    throw std::runtime_error("character_floating_state_creation");
                }
                reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component,
                                                                                     0.0f);
            }
        }
        if (falling && is_uobject_valid_fast(movement))
        {
            Call mode(movement, L"SetMovementMode");
            mode.integer(L"NewMovementMode", 3);
            mode.integer(L"NewCustomMode", 0);
            mode.run();
        }
        // Apply after the factory and movement-mode setup. Copy both
        // values so grounded swaps also replace a parked pawn's stale
        // spent flag. The game's normal landing path still rearms it.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        reinterpret_cast<void (*)(UObject*, bool)>(base + set_air_jump_available_rva)(
            pawn, air_jump_available);
        character_traversal::apply_movement(pawn);
    }

    bool CharacterCombatSwap::Impl::position_incoming_pawn(UObject* pawn)
    {
        if (floating_mode && character_swap::owns_swap_pawn(pawn) &&
            (!character_exploration::prepare_pawn(pawn) || !character_traversal::prepare(pawn)))
        {
            // A fresh alternate may omit Floating from its authored
            // state set. Bind the incoming queue and motions before
            // the same transaction requests that shared state.
            return false;
        }
        auto* camera = object(camera_manager(pawn), L"m_CameraNormal1st");
        if (!is_uobject_valid_fast(object(camera, L"RootComponent")))
        {
            return false;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        camera_control_ready = available(pawn);
        camera_ui_current = player() == pawn;
#endif
        Call position(pawn, L"K2_SetActorLocationAndRotation");
        position.vector(L"NewLocation", location[0], location[1], location[2]);
        position.vector(L"NewRotation", rotation[0], rotation[1], rotation[2]);
        position.boolean(L"bSweep", false);
        position.boolean(L"bTeleport", true);
        position.run();
        auto* movement = object(pawn, L"MyMovement");
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        pose_handoff = {{"grounded", grounded},
                        {"falling", falling},
                        {"floating", floating_mode},
                        {"source_velocity", velocity},
                        {"before_state", state_id(state(pawn))},
                        {"before_mode", property_value<uint8_t>(movement, L"MovementMode")}};
#endif
        restore_movement_mode(pawn, movement);
        auto* speed = is_uobject_valid_fast(movement)
                          ? movement->GetPropertyByNameInChain(L"Velocity")
                          : nullptr;
        if (speed && speed->GetElementSize() == sizeof(velocity))
        {
            std::memcpy(speed->ContainerPtrToValuePtr<void>(movement), velocity.data(),
                        sizeof(velocity));
        }
        restore_movement(pawn);
        // The factory can leave the pooled pawn's old current state
        // until its next queue tick. Consume that pending transition
        // now, with restored movement/input and no parked-pose blend.
        auto* component = object(pawn, L"MyStateComp");
        if (!is_uobject_valid_fast(component) ||
            component->GetClassPrivate()->GetName() != L"TresStateQueueComponent")
        {
            throw std::runtime_error("character_handoff_state_queue");
        }
        // Finish the parked state's exit first. Air attacks restore
        // Falling during their exit, even after the factory selected
        // Walking, so the grounded reset must follow that cleanup.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const bool pending_run_handoff = run_handoff;
        reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component, 0.0f);
        if (grounded && !floating_mode && property_value<uint8_t>(movement, L"MovementMode") != 1)
        {
            run_handoff = pending_run_handoff;
            // A canceled airborne attack can leave Jump queued behind
            // it. Discard that old journey through the normal lifecycle
            // before entering the source's grounded locomotion.
            reinterpret_cast<void (*)(UObject*)>(base + queue_clear_rva)(component);
            Call mode(movement, L"SetMovementMode");
            mode.integer(L"NewMovementMode", 1);
            mode.integer(L"NewCustomMode", 0);
            mode.run();
            auto* idle = reinterpret_cast<UObject* (*)(UObject*, int)>(base + queue_push_rva)(
                component, player_state_ids::idle);
            if (!is_uobject_valid_fast(idle) || object(idle, L"MyCharPawn") != pawn)
            {
                throw std::runtime_error("character_handoff_idle_state_creation");
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            pose_handoff["ground_mode"] = property_value<uint8_t>(movement, L"MovementMode");
            pose_handoff["ground_state"] = state_id(state(pawn));
#endif
            reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component, 0.0f);
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        pose_handoff["updated_mode"] = property_value<uint8_t>(movement, L"MovementMode");
        pose_handoff["updated_state"] = state_id(state(pawn));
#endif
        // If cleanup already entered Run, finish its pose without
        // clearing and reusing the same active state a second time.
        run_pose_pending = pending_run_handoff && !run_handoff;
        resume_swing(pawn);
        first_movement_pending =
            run_handoff && run_loop && !falling &&
            stick_distance_squared(stick_input.processed, {}) > movement_intent_epsilon_squared &&
            stick_distance_squared(stick_input.raw, {}) > movement_intent_epsilon_squared &&
            stick_distance_squared(stick_input.raw, stick_input.committed_raw) <=
                stick_continuity_epsilon_squared &&
            velocity[0] * velocity[0] + velocity[1] * velocity[1] > run_speed_epsilon_squared;
        if (first_movement_pending)
        {
            first_movement_pawn = make_uobject_weak(pawn);
            first_movement_location = vector(pawn, L"K2_GetActorLocation");
            first_movement_deadline = Clock::now() + first_movement_grace;
        }
        positioned = make_uobject_weak(pawn);
        return true;
    }

    void CharacterCombatSwap::Impl::restore_movement(UObject* pawn)
    {
        auto* movement = object(pawn, L"MyMovement");
        // These are the live limits, including an established sprint, not
        // the newly constructed pawn's default walking limits.
        set_property(movement, L"MaxWalkSpeed", movement_max_speed);
        set_property(movement, L"MaxAcceleration", movement_max_acceleration);
        set_property(movement, L"Velocity", velocity);
        set_property(movement, L"LastUpdateVelocity", velocity);
        set_property(movement, L"Acceleration", acceleration);
        set_property(movement, L"AnalogInputModifier", analog_modifier);
        set_property(pawn, L"m_ControlAnalogInputModifier", control_modifier);
        const float pending_size =
            movement_input[0] * movement_input[0] + movement_input[1] * movement_input[1];
        set_property(pawn, L"ControlInputVector",
                     pending_size > movement_intent_epsilon_squared ? movement_input
                                                                    : last_movement_input);
        set_property(pawn, L"LastControlInputVector", last_movement_input);
    }

    void CharacterCombatSwap::Impl::enter_run(UObject* current)
    {
        auto* self = active;
        reinterpret_cast<void (*)(UObject*)>(self->original_run)(current);
        try
        {
            auto* pawn = object(current, L"MyCharPawn");
            if (!self->run_handoff || Clock::now() > self->run_until ||
                !is_uobject_valid_fast(pawn) || player_id(pawn) != self->target ||
                pawn == self->outgoing.Get() ||
                actor_owning_world(pawn) != self->request_world.Get() ||
                local_player_pawn(self->request_world.Get()) != pawn ||
                current->GetClassPrivate()->GetName() != L"TresPlayerState_Run")
            {
                return;
            }
            self->run_handoff = false;
            auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            const auto motion = resolve_run_motion(anim, self->run_animation);
            // Resume the established locomotion cycle, bypassing the entry
            // montage that suppresses movement until its startup completes.
            set_property(current, L"m_nSubStep", self->run_step);
            auto* bytes = reinterpret_cast<std::byte*>(current);
            std::memcpy(bytes + 0x8c, &self->run_heading, sizeof(float));
            // Run's walk latch and its release grace select the native
            // walk/run loop and analog limit. A fresh state's defaults can
            // briefly select full-speed running despite a partial stick.
            std::memcpy(bytes + run_walk_active_offset, &self->run_walk_active, sizeof(uint8_t));
            std::memcpy(bytes + run_walk_input_offset, &self->run_walk_input, sizeof(uint8_t));
            std::memcpy(bytes + run_walk_grace_offset, &self->run_walk_grace, sizeof(float));
            std::memcpy(bytes + 0xa0, &motion, sizeof(FName));
            std::memcpy(bytes + 0xb8, &self->run_elapsed, sizeof(float));
            *reinterpret_cast<int*>(bytes + 0xd0) = 0;
            auto* flags = reinterpret_cast<uint32_t*>(bytes + 0xd4);
            *flags = (*flags & ~0xau) | (self->run_flags & 0xau);
            Call play(pawn, L"BP_AnimPlayAnimset");
            play.name(L"InAnimName", motion.ToString().c_str());
            play.name(L"InSlotNodeName", L"FullBody");
            play.number(L"InBlendInTime", 0.0f);
            play.boolean(L"InLoop", self->run_loop);
            play.boolean(L"InRootTrans", (self->run_flags & 2u) != 0);
            play.number(L"InPlayRate", self->run_play_rate);
            play.number(L"InRootTransScaleXY", 1.0f);
            play.number(L"InRootTransScaleZ", 1.0f);
            play.run();
            self->restore_movement(pawn);
        }
        catch (const std::exception& failure)
        {
            self->error = failure.what();
        }
    }

    bool CharacterCombatSwap::Impl::resume_run(UObject* pawn)
    {
        if ((!run_handoff && !run_pose_pending) || swing_pending)
        {
            return false;
        }
        auto* component = object(pawn, L"MyStateComp");
        if (!is_uobject_valid_fast(component) ||
            component->GetClassPrivate()->GetName() != L"TresStateQueueComponent")
        {
            throw std::runtime_error("character_run_state_queue");
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // As with a combo handoff, enter through the native queue lifecycle.
        // Waiting for the initial Idle state costs a movement frame and can
        // apply braking before Run has a chance to restore its progress.
        auto* run = state(pawn);
        if (run_handoff)
        {
            reinterpret_cast<void (*)(UObject*)>(base + queue_clear_rva)(component);
            run = reinterpret_cast<UObject* (*)(UObject*, int)>(base + queue_push_rva)(
                component, player_state_ids::run);
        }
        if (!is_uobject_valid_fast(run) || object(run, L"MyCharPawn") != pawn ||
            run->GetClassPrivate()->GetName() != L"TresPlayerState_Run")
        {
            throw std::runtime_error("character_run_state_creation");
        }
        if (run_handoff)
        {
            reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component, 0.0f);
        }
        if (run_handoff || state(pawn) != run)
        {
            return false;
        }
        run_pose_pending = false;
        // The queue's initial Run update can choose another stance with
        // its ordinary blend, replacing enter_run's zero-blend request.
        // Restart that final motion without blending the parked pose in,
        // then seek it before the next mesh update.
        auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
        const auto motion = field<FName>(run, 0xa0);
        Call play(pawn, L"BP_AnimPlayAnimset");
        play.name(L"InAnimName", motion.ToString().c_str());
        play.name(L"InSlotNodeName", L"FullBody");
        play.number(L"InBlendInTime", 0.0f);
        play.boolean(L"InLoop", run_loop);
        play.boolean(L"InRootTrans", (field<uint32_t>(run, 0xd4) & 2u) != 0);
        play.number(L"InPlayRate", run_play_rate);
        play.number(L"InRootTransScaleXY", 1.0f);
        play.number(L"InRootTransScaleZ", 1.0f);
        play.run();
        const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
        if (!std::isfinite(length) || length <= 0.0f)
        {
            return true;
        }
        Call phase(anim, L"SQEX_AnimSetCurrentTime");
        phase.name(L"InSlotName", L"FullBody");
        phase.number(L"InTime", run_phase * length);
        phase.run();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (session.test_locomotion && session.test_phase != 0)
        {
            session.test_result["run_resumes"].push_back(
                {{"player", player_id(pawn)},
                 {"source_phase", run_phase},
                 {"requested_play_rate", run_play_rate},
                 {"play_rate", animation_value(anim, L"SQEX_AnimGetCurrentPlayRate")},
                 {"time", animation_value(anim, L"SQEX_AnimGetCurrentTime")},
                 {"length", length},
                 {"motion", to_utf8(field<FName>(run, 0xa0).ToString())}});
        }
#endif
        return true;
    }

    void CharacterCombatSwap::Impl::refresh_handoff_pose(UObject* pawn)
    {
        // This transaction runs after the engine's mesh tick. Evaluate the
        // incoming pose now, at zero delta, so its parked bone transforms
        // cannot be submitted for the first visible frame. Passing no tick
        // function makes the normal refresh finish synchronously.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto* mesh = object(pawn, L"MyMesh");
        const auto refresh =
            base + character_native::rva(character_native::Site::skeletal_refresh_bones);
        constexpr size_t refresh_bones_vtable_offset = 0xac0;
        if (!is_uobject_valid_fast(mesh) ||
            field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(mesh, 0)),
                             refresh_bones_vtable_offset) != refresh)
        {
            throw std::runtime_error("character_handoff_pose_mesh_layout");
        }
        using TickAnimation = void (*)(UObject*, float, bool);
        using RefreshBones = void (*)(UObject*, void*);
        const auto tick_animation =
            base + character_native::rva(character_native::Site::skeletal_tick_animation);
        reinterpret_cast<TickAnimation>(tick_animation)(mesh, 0.0f, false);
        reinterpret_cast<RefreshBones>(refresh)(mesh, nullptr);
    }

    void CharacterCombatSwap::Impl::capture_frame(UObject* pawn)
    {
        // Doubleflight belongs to this airborne journey, not to the pawn
        // being created or woken by the character factory.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        air_jump_available =
            reinterpret_cast<bool (*)(UObject*)>(base + air_jump_available_rva)(pawn);
        auto* current = state(pawn);
        // Native Sora NotifySetFloatingMode owns the low two shared pawn
        // bits (IsOn/IsSpecial). Preserve the active encounter on handoff.
        floating_mode = kh02_current_story_world() ? 0u : field<uint32_t>(pawn, 0xd80) & 3u;
        swing_pending = false;
        if (attack_state(current, pawn))
        {
            auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
            const float time = animation_value(anim, L"SQEX_AnimGetCurrentTime");
            const float play_rate = animation_value(anim, L"SQEX_AnimGetCurrentPlayRate");
            swing_index = field<int>(current, combo_index_offset);
            swing_phase = length > 0.0f ? std::clamp(time / length, 0.0f, 1.0f) : 1.0f;
            swing_progress_rate = length > 0.0f ? play_rate / length : 0.0f;
            swing_air = field<uint8_t>(current, combo_air_offset) != 0;
            swing_input_ready = state_boolean(current, L"m_bEnableInput");
            swing_change_ready = state_boolean(current, L"m_bEnableChange");
            swing_pending = swing_index >= 0 && swing_index < 64 && swing_phase < 0.995f &&
                            std::isfinite(swing_progress_rate) && swing_progress_rate > 0.0f;
            swing_target = make_uobject_weak(object(current, L"m_pTargetPawn"));
            swing_homing = make_uobject_weak(object(current, L"m_HomingTargetComponent"));
        }
        location = vector(pawn, L"K2_GetActorLocation");
        rotation = vector(pawn, L"K2_GetActorRotation");
        velocity = vector(pawn, L"GetVelocity");
        control_rotation = vector(object(pawn, L"Controller"), L"GetControlRotation");
        rendered_view = field<CameraView>(camera_pov(camera_manager(pawn), L"CameraCache"), 0);
        rendered_timestamp = *camera_timestamp(camera_manager(pawn));
        zoom = camera_handoff_distance(pawn);
        orbit = camera_orbit(pawn);
        Call frame_delta(L"/Script/Engine.Default__GameplayStatics", L"GetWorldDeltaSeconds");
        frame_delta.object(L"WorldContextObject", pawn);
        frame_delta.run();
        camera_frame_delta =
            std::clamp(*frame_delta.param(L"ReturnValue")
                            ->ContainerPtrToValuePtr<float>(frame_delta.parameters.data()),
                       1.0f / 240.0f, 1.0f / 30.0f);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        camera_deltas = nlohmann::json::array();
#endif
        stick_input = runtime_controller_capture_stick(object(pawn, L"Controller"), false);
        look_input = runtime_controller_capture_stick(object(pawn, L"Controller"), true);
        auto* movement = object(pawn, L"MyMovement");
        movement_input = property_value<std::array<float, 3>>(pawn, L"ControlInputVector");
        last_movement_input = property_value<std::array<float, 3>>(pawn, L"LastControlInputVector");
        acceleration = property_value<std::array<float, 3>>(movement, L"Acceleration");
        movement_max_speed = property_value<float>(movement, L"MaxWalkSpeed");
        movement_max_acceleration = property_value<float>(movement, L"MaxAcceleration");
        analog_modifier = property_value<float>(movement, L"AnalogInputModifier");
        control_modifier = property_value<float>(pawn, L"m_ControlAnalogInputModifier");
        run_step = property_value<int>(current, L"m_nSubStep");
        const bool running = state_id(current) == player_state_ids::run;
        const auto motion_name = running ? field<FName>(current, 0xa0).ToString() : std::wstring{};
        const bool starting_motion =
            motion_name.starts_with(L"RunStart") || motion_name.starts_with(L"WalkStart");
        const bool looping_motion =
            motion_name.starts_with(L"RunLoop") || motion_name.starts_with(L"WalkLoop");
        const float pending_movement =
            movement_input[0] * movement_input[0] + movement_input[1] * movement_input[1];
        const float previous_movement = last_movement_input[0] * last_movement_input[0] +
                                        last_movement_input[1] * last_movement_input[1];
        const float stick_movement = stick_input.processed[0] * stick_input.processed[0] +
                                     stick_input.processed[1] * stick_input.processed[1];
        run_handoff =
            running && (starting_motion || looping_motion) &&
            (run_step == run_loop_phase || run_step == run_entry_phase ||
             (run_step == run_stance_blend_phase && looping_motion)) &&
            (velocity[0] * velocity[0] + velocity[1] * velocity[1] > run_speed_epsilon_squared ||
             pending_movement > movement_intent_epsilon_squared ||
             previous_movement > movement_intent_epsilon_squared ||
             stick_movement > movement_intent_epsilon_squared);
        if (run_handoff)
        {
            // Phase 3 blends a hero's battle/normal run loops. The incoming
            // hero must choose its own stance without falling back to Idle.
            if (run_step == run_stance_blend_phase)
            {
                run_step = run_loop_phase;
            }
            run_heading = field<float>(current, 0x8c);
            run_walk_active = field<uint8_t>(current, run_walk_active_offset);
            run_walk_input = field<uint8_t>(current, run_walk_input_offset);
            run_walk_grace = field<float>(current, run_walk_grace_offset);
            run_elapsed = field<float>(current, 0xb8);
            run_flags = field<uint32_t>(current, 0xd4);
            run_animation = field<FName>(current, 0xa0);
            auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
            const float time = animation_value(anim, L"SQEX_AnimGetCurrentTime");
            run_play_rate = animation_value(anim, L"SQEX_AnimGetCurrentPlayRate");
            // The source rate already includes its character's stride
            // correction. Transfer the underlying rate so the receiving
            // play hook applies its own correction exactly once; otherwise
            // Kairi's Aqua loop multiplier compounds on every round trip.
            run_play_rate /= character_idle::movement_play_rate_scale(anim, run_animation);
            Call loop(anim, L"SQEX_IsAnimLoop");
            loop.name(L"InSlotName", L"FullBody");
            loop.run();
            // A stance blend can report a non-looping slot while the
            // selected motion is already the next authored run loop.
            run_loop = looping_motion || loop.boolean_result();
            run_phase = length > 0.0f ? (run_loop ? std::fmod(time / length, 1.0f)
                                                  : std::clamp(time / length, 0.0f, 1.0f))
                                      : 0.0f;
            run_until = Clock::now() + switch_timeout;
        }
        auto* mode = is_uobject_valid_fast(movement)
                         ? movement->GetPropertyByNameInChain(L"MovementMode")
                         : nullptr;
        falling = mode && mode->GetElementSize() == 1 &&
                  *mode->ContainerPtrToValuePtr<uint8_t>(movement) == 3;
        grounded = mode && mode->GetElementSize() == 1 &&
                   *mode->ContainerPtrToValuePtr<uint8_t>(movement) == 1;
    }

    void CharacterCombatSwap::Impl::complete_first_movement(Clock::time_point now)
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        const character_swap_trace::NativeScope trace{"first_movement"};
#endif
        if (!first_movement_pending)
        {
            return;
        }
        first_movement_pending = false;
        auto* pawn = first_movement_pawn.Get();
        if (now > first_movement_deadline || request_world.Get() != swap_context_world() ||
            !is_uobject_valid_fast(pawn) || local_player_pawn(request_world.Get()) != pawn ||
            state_id(state(pawn)) != player_state_ids::run || !available(pawn))
        {
            return;
        }
        auto* movement = object(pawn, L"MyMovement");
        if (!is_uobject_valid_fast(movement) ||
            property_value<uint8_t>(movement, L"MovementMode") != walking_movement_mode)
        {
            return;
        }
        const auto current_location = vector(pawn, L"K2_GetActorLocation");
        float distance_squared{};
        for (size_t axis = 0; axis < current_location.size(); ++axis)
        {
            const float delta = current_location[axis] - first_movement_location[axis];
            distance_squared += delta * delta;
        }
        if (distance_squared > movement_intent_epsilon_squared)
        {
            return;
        }
        const auto input = runtime_controller_capture_stick(object(pawn, L"Controller"), false);
        if (stick_distance_squared(input.processed, stick_input.processed) >
                stick_continuity_epsilon_squared ||
            stick_distance_squared(input.raw, stick_input.raw) > stick_continuity_epsilon_squared)
        {
            return;
        }
        const auto current_velocity = vector(pawn, L"GetVelocity");
        if (current_velocity[0] * current_velocity[0] + current_velocity[1] * current_velocity[1] <=
            run_speed_epsilon_squared)
        {
            return;
        }
        Call frame(L"/Script/Engine.Default__GameplayStatics", L"GetWorldDeltaSeconds");
        frame.object(L"WorldContextObject", pawn);
        frame.run();
        const float delta =
            *frame.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(frame.parameters.data());
        if (!std::isfinite(delta) || delta <= 0.0f || delta > maximum_first_movement_delta_seconds)
        {
            return;
        }
        // A newly possessed character's first movement tick can initialize
        // its history without advancing. Complete only that missing ground
        // step through the owning movement component's collision sweep.
        // Never add velocity, teleport, repeat after normal movement, or
        // apply this to root-motion startup, falling, swimming or new input.
        const auto angles = vector(pawn, L"K2_GetActorRotation");
        Call move(movement, L"K2_MoveUpdatedComponent");
        move.vector(L"Delta", current_velocity[0] * delta, current_velocity[1] * delta, 0.0f);
        move.vector(L"NewRotation", angles[0], angles[1], angles[2]);
        move.boolean(L"bSweep", true);
        move.boolean(L"bTeleport", false);
        move.run();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ++first_movement_updates;
#endif
    }
} // namespace kh3ap
