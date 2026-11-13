#include "kh3ap/features/player/characters/character_layout.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/combat/character_underwater_combat.h"
#include "kh3ap/features/player/combat/character_union_cross.h"
#include "kh3ap/features/player/movement/character_rails.h"

// PolyHook must precede Unreal headers to avoid their assembler macro names.
#include <polyhook2/Detour/x64Detour.hpp>

#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/player/player_animation_support.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_traversal
{
    using namespace RC::Unreal;
    namespace
    {
        using player_animation_support::AnimationEntry;
        constexpr std::array movement_fields{L"MaxWalkSpeed",
                                             L"MaxAcceleration",
                                             L"GroundFriction",
                                             L"WalkSpeedRate",
                                             L"BrakingFrictionFactor",
                                             L"BrakingFriction",
                                             L"BrakingDecelerationWalking",
                                             L"BrakingDecelerationFalling",
                                             L"FallingLateralFriction",
                                             L"MaxSwimSpeed",
                                             L"BrakingDecelerationSwimming"};
        constexpr std::array pawn_fields{L"MyAthleticLockonLimit", L"MyAthleticLockonRange",
                                         L"MyAthleticSlideTime", L"MyAthleticSlideSpeed", L"MyAthleticSlideKickAnim",
                                         L"MyAthleticAttackSlowRate", L"MyAthleticAttackSlowTime",
                                         L"MyFloatingSpeed", L"MyFloatingGravityScale", L"MyFloatingGravityScale2",
                                         L"MyGlideGravityScale", L"MyGlideSpeed", L"MyGlideTurnRate",
                                         L"MySprintSpeed",
                                         L"MyCoverFindLimit", L"MyCoverMoveSpeed", L"MyCoverSlideTime",
                                         L"MyCoverSlideSpeed", L"m_CoverSlideMinSpeed", L"MyCoverSlideBreak",
                                         L"MyCoverSlideMutekiTime", L"MyCoverCommandRreadyTime",
                                         L"MyCoverSlideSlowRate", L"MyCoverSlideSlowTime",
                                         L"MyCoverShotSlideTime", L"MyCoverShotSlideSpeed",
                                         L"m_CoverShotSlideMinSpeed", L"MyCoverShotSlideBreak",
                                         L"MyCoverShotMutekiTime", L"MyCoverShotSlideHomingRate",
                                         L"MyCoverShotLockonLimit", L"MyCoverShotLockonSightMoveRate",
                                         L"MyRailSlideCheckRange", L"MyRailSlideCheckAirRange",
                                         L"MyRailSlideShotInterval", L"MyRailSlideSpeed",
                                         L"m_RailSlideJumpInertialVelocity", L"m_RailSlideJumpInertialBrake",
                                         L"MyHopEffectRange",
                                         L"MySprintDelay",
                                         L"MyForceWalkSpeed",
                                         L"MySwimSurfaceBuoyancy",
                                         L"MySwimSurfaceSpeed",
                                         L"MySwimDiveLowSpeed",
                                         L"MySwimDiveHighSpeed",
                                         L"MySwimDiveDiveSpeed",
                                         L"MySwimDiveRiseSpeed",
                                         L"MySwimDashSpeed",
                                         L"MySwimDashTime",
                                         L"MySwimDiveOxygenTime",
                                         L"MySwimDiveDashOxygenConsumeRate",
                                         L"MySwimDiveDashDisableInputTime",
                                         L"MySwimDivingHighSpeedMoveRotSpeedYaw",
                                         L"MySwimDivingHighSpeedMoveRotSpeedPitch"};
        constexpr std::array animation_names{
            L"MagicCast", L"MagicCastM", L"MagicChant", L"MagicFire",
            L"CoverIdle", L"CoverIdleR", L"CoverIdleL", L"CoverMove",
            L"CoverSlideLoop", L"CoverSlideEnd", L"CoverSlideShot", L"CoverSlideShotEnd",
            L"GigasStart", L"GigasEnd",
            L"MagicFireMF", L"MagicFireML", L"MagicFireMR", L"MagicShoot",
            L"MagicAirCast", L"MagicAirCastM", L"MagicAirChant", L"MagicAirFire",
            L"MagicAirFireMF", L"MagicAirFireML", L"MagicAirFireMR", L"MagicAirShoot",
            L"BadStatBurnStart", L"BadStatBurnLoop", L"BadStatBurnEnd",
            L"BadStatSneeze", L"BadStatAirSneeze",
            L"GlideLoop", L"GlideLoopF", L"GlideLoopS", L"GlideLoopR", L"GlideLoopL", L"GlideIdle",
            L"AthleticSlide", L"AthleticSlide2", L"AthleticSlide3", L"AthleticSlideEnd",
            L"SlideEnd", L"WallKickF",
            L"RailSlideStart", L"RailSlideLoop", L"RailSlideNarrowLoop",
            L"RailSlideStartS", L"RailSlideLoopS", L"RailSlideShotS", L"RailSlideShotSEnd",
            L"RailSlideTurn", L"RailSlideTransfer",
            L"UCrossIdle", L"UCrossEndureStart", L"UCrossEndureLoop", L"UCrossEndureEnd",
            L"UCrossCombo1", L"UCrossCombo2", L"UCrossCombo3", L"UCrossCombo4", L"UCrossCombo5",
            L"UCrossFinishIdle",
            L"ShootFlowStart", L"ShootFlowIdle", L"ShootFlowEnd",
            L"OpenTreasureS", L"OpenTreasureL", L"Diving",       L"SwimIdle",
            L"SwimNormal",    L"SwimTurn",      L"HopR",         L"HopL",
            L"PoleTurn",      L"PoleTurnLoop",  L"PoleSwing",    L"PoleSwingLoop",
            L"SwimToDive",    L"DiveIdle",      L"DiveMoveHigh", L"DiveDash", L"ODanceSpecialAct1",
            L"M_1088", L"M_1089", L"M_1090", L"M_1091",
            L"SelfieStart",
            L"SelfieStartSea",
            L"SelfieLoop",
            L"SelfieLoopSea",
            L"SelfiePoseChange1",
            L"SelfiePoseChange1Sea",
            L"SelfiePoseChange2",
            L"SelfiePoseChange2Sea",
            L"SelfieQRStart",
            L"SelfieQREnd",
            L"SelfiePalmStart",
            L"SelfiePalmStartSea",
            L"SelfiePalmLoop",
            L"SelfiePalmLoopSea",
            L"SelfiePalmEnd",
            L"SelfiePalmEndSea",
            L"SelfiePinchStart",
            L"SelfiePinchStartSea",
            L"SelfiePinchLoop",
            L"SelfiePinchLoopSea",
            L"SelfiePinchEnd",
            L"SelfiePinchEndSea",
            L"SelfieBrowStart",
            L"SelfieBrowStartSea",
            L"SelfieBrowLoop",
            L"SelfieBrowLoopSea",
            L"SelfieBrowEnd",
            L"SelfieBrowEndSea"};
        constexpr auto caribbean_animation_set_path =
            character_underwater_combat::animation_set_path;
        constexpr float maximum_tuning_value = 10000000.0f;
        std::array<float, movement_fields.size()> sora_movement{};
        std::array<float, pawn_fields.size()> sora_pawn{};
        bool tuning_captured{};
        FWeakObjectPtr prepared_pawn;
        FWeakObjectPtr prepared_animation;
        FWeakObjectPtr sora_hop_effect;
        FWeakObjectPtr sora_athletic_root;
        FWeakObjectPtr sora_athletic_hit_effect;
        const uintptr_t default_walk_speed_rva = character_native::rva(character_native::Site::default_walk_speed);
        const uintptr_t reset_walk_speed_rva = character_native::rva(character_native::Site::reset_walk_speed);
        const uintptr_t request_swim_rva = character_native::rva(character_native::Site::request_swim);
        RuntimeOptionsInputOwnership swim_input;
        FWeakObjectPtr swim_input_pawn;
        bool descend_held{}, ascend_held{}, dash_held{}, dive_requested{};
        const uintptr_t swim_update_rva = character_native::rva(character_native::Site::swim_update);
        constexpr size_t state_owner_offset = 0x68;
        constexpr size_t state_phase_offset = 0x78;
        uint64_t original_swim_update{};
        std::unique_ptr<PLH::x64Detour> swim_update_hook;
        FWeakObjectPtr swim_failure_pawn;
        void update_swim_input(UObject *pawn, UObject *state);

        void update_swim(UObject *state, float delta)
        {
            if (is_uobject_valid_fast(state))
            {
                auto *pawn = *reinterpret_cast<UObject **>(reinterpret_cast<uint8_t *>(state) +
                                                           state_owner_offset);
                if (prepared_pawn.Get() == pawn && character_swap::owns_swap_pawn(pawn))
                {
                    try
                    {
                        update_swim_input(pawn, state);
                    }
                    catch (const std::exception &failure)
                    {
                        if (swim_failure_pawn.Get() != pawn)
                        {
                            swim_failure_pawn = make_uobject_weak(pawn);
                            const std::string message = failure.what();
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[KH3APProbeCpp] Character swim input failed: {}\n"),
                                std::wstring(message.begin(), message.end()));
                        }
                    }
                }
            }
            reinterpret_cast<void (*)(UObject *, float)>(original_swim_update)(state, delta);
        }
        using RequestSwim = bool (*)(void *, float, float, float, float, float, bool, bool, bool,
                                     bool, bool);
        uint64_t original_default_walk{}, original_reset_walk{};
        std::unique_ptr<PLH::x64Detour> default_walk_hook, reset_walk_hook;
        float sora_default_walk{};

        bool owns_movement(UObject *pawn)
        {
            return tuning_captured && prepared_pawn.Get() == pawn &&
                   character_swap::owns_swap_pawn(pawn);
        }

        float default_walk_speed(UObject *pawn)
        {
            return owns_movement(pawn)
                       ? sora_default_walk
                       : reinterpret_cast<float (*)(UObject *)>(original_default_walk)(pawn);
        }

        void reset_walk_speed(UObject *pawn)
        {
            reinterpret_cast<void (*)(UObject *)>(original_reset_walk)(pawn);
            if (!owns_movement(pawn))
            {
                return;
            }
            auto *function = pawn->GetFunctionByNameInChain(L"SetMaxWalkSpeed");
            if (is_uobject_valid_fast(function) && function->GetParmsSize() == sizeof(float))
            {
                // Native resets can inline their class-default read. Follow
                // the reset with the owning setter so its acceleration update
                // uses Sora's speed too, without modifying any class defaults.
                float speed = sora_default_walk;
                pawn->ProcessEvent(function, &speed);
            }
        }

        UObject *object(UObject *owner, const wchar_t *field)
        {
            return is_uobject_valid_fast(owner)
                       ? try_read_reflected_object_property_value(owner, field)
                       : nullptr;
        }

        UObject* athletic_reference(UObject* pawn, const wchar_t* field, const wchar_t* kind)
        {
            auto* property = is_uobject_valid_fast(pawn) ? pawn->GetPropertyByNameInChain(field) : nullptr;
            if (!property || property->GetClass().GetName() != kind ||
                property->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("character_airstep_effect_layout");
            }
            auto* value = *property->ContainerPtrToValuePtr<UObject*>(pawn);
            if (!is_uobject_valid_fast(value))
            {
                throw std::runtime_error("character_airstep_effect_missing");
            }
            return value;
        }

        float *number(UObject *owner, const wchar_t *field)
        {
            auto *property =
                is_uobject_valid_fast(owner) ? owner->GetPropertyByNameInChain(field) : nullptr;
            if (!property || property->GetClass().GetName() != L"FloatProperty" ||
                property->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("character_movement_tuning_layout");
            }
            return property->ContainerPtrToValuePtr<float>(owner);
        }

        template <size_t N>
        void capture(UObject *owner, const std::array<const wchar_t *, N> &fields,
                     std::array<float, N> &values)
        {
            for (size_t index = 0; index < fields.size(); ++index)
            {
                const float value = *number(owner, fields[index]);
                if (!std::isfinite(value) || value < 0.0f || value > maximum_tuning_value)
                {
                    throw std::runtime_error("character_movement_tuning_range");
                }
                values[index] = value;
            }
        }

        template <size_t N>
        void apply(UObject *owner, const std::array<const wchar_t *, N> &fields,
                   const std::array<float, N> &values)
        {
            for (size_t index = 0; index < fields.size(); ++index)
            {
                *number(owner, fields[index]) = values[index];
            }
        }

        TArray<AnimationEntry> *animations(UObject *set)
        {
            auto *field =
                is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty *>(field)->GetInner()->GetElementSize() !=
                    sizeof(AnimationEntry))
            {
                throw std::runtime_error("character_traversal_animset_layout");
            }
            return field->ContainerPtrToValuePtr<TArray<AnimationEntry>>(set);
        }
    } // namespace

    bool initialize()
    {
        if (default_walk_hook && reset_walk_hook && swim_update_hook)
        {
            return true;
        }
        if (!player_animation_support::supported_skeleton_layout())
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!character_native::verify_sites({character_native::Site::default_walk_speed, character_native::Site::reset_walk_speed, character_native::Site::request_swim, character_native::Site::swim_update}))
        {
            return false;
        }
        default_walk_hook = std::make_unique<PLH::x64Detour>(
            base + default_walk_speed_rva, reinterpret_cast<uintptr_t>(&default_walk_speed),
            &original_default_walk);
        reset_walk_hook = std::make_unique<PLH::x64Detour>(
            base + reset_walk_speed_rva, reinterpret_cast<uintptr_t>(&reset_walk_speed),
            &original_reset_walk);
        swim_update_hook = std::make_unique<PLH::x64Detour>(
            base + swim_update_rva, reinterpret_cast<uintptr_t>(&update_swim),
            &original_swim_update);
        if (!default_walk_hook->hook() || !original_default_walk || !reset_walk_hook->hook() ||
            !original_reset_walk || !swim_update_hook->hook() || !original_swim_update)
        {
            swim_update_hook.reset();
            reset_walk_hook.reset();
            default_walk_hook.reset();
            return false;
        }
        return true;
    }

    void capture_sora(UObject *pawn)
    {
        tuning_captured = false;
        capture(object(pawn, L"MyMovement"), movement_fields, sora_movement);
        capture(pawn, pawn_fields, sora_pawn);
        auto *default_speed = pawn->GetFunctionByNameInChain(L"GetDefaultMaxWalkSpeed");
        if (!is_uobject_valid_fast(default_speed) || default_speed->GetParmsSize() != sizeof(float))
        {
            throw std::runtime_error("character_default_walk_layout");
        }
        pawn->ProcessEvent(default_speed, &sora_default_walk);
        if (!std::isfinite(sora_default_walk) || sora_default_walk <= 0 ||
            sora_default_walk > maximum_tuning_value)
        {
            throw std::runtime_error("character_default_walk_range");
        }
        sora_hop_effect = make_uobject_weak(object(pawn, L"m_pHopEffect"));
        // The root is a ClassProperty, which the ordinary object reader rejects.
        // Validate both references before retiring Sora's pawn.
        sora_athletic_root = make_uobject_weak(athletic_reference(pawn, L"m_AthleticRoot", L"ClassProperty"));
        sora_athletic_hit_effect = make_uobject_weak(athletic_reference(pawn, L"m_pAthleticHitEffect", L"ObjectProperty"));
        tuning_captured = true;
    }

    void apply_movement(UObject *pawn)
    {
        if (!tuning_captured || !character_swap::owns_swap_pawn(pawn) ||
            !is_uobject_valid_fast(pawn))
        {
            return;
        }
        auto *id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != sizeof(uint8_t))
        {
            return;
        }
        const auto player = *id->ContainerPtrToValuePtr<uint8_t>(pawn);
        using player_animation_support::PlayerId;
        if (player != static_cast<uint8_t>(PlayerId::Aqua) &&
            player != static_cast<uint8_t>(PlayerId::Riku) &&
            player != static_cast<uint8_t>(PlayerId::Kairi) &&
            player != static_cast<uint8_t>(PlayerId::Roxas))
        {
            return;
        }
        // Copy tuning as well as the instantaneous velocity/acceleration restored
        // by the combat handoff. Apply before its first input and run-state update.
        apply(object(pawn, L"MyMovement"), movement_fields, sora_movement);
        apply(pawn, pawn_fields, sora_pawn);
    }

    bool ready(UObject *pawn)
    {
        return pawn && prepared_pawn.Get() == pawn && prepared_animation.Get() &&
               prepared_animation.Get() == object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
    }

    bool rail_ready(UObject *pawn)
    {
        if (!ready(pawn) || !character_swap::owns_swap_pawn(pawn))
        {
            return false;
        }
        auto *field = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
        if (!field || field->GetElementSize() != sizeof(uint8_t))
        {
            return false;
        }
        const auto player = static_cast<PlayerId>(*field->ContainerPtrToValuePtr<uint8_t>(pawn));
        return player == PlayerId::Aqua || player == PlayerId::Kairi || player == PlayerId::Roxas;
    }

    bool barrel_spin_ready(UObject *pawn)
    {
        if (!ready(pawn))
        {
            return false;
        }
        auto *instance = prepared_animation.Get();
        auto *field = instance->GetPropertyByNameInChain(L"MyAnimSets");
        if (!field || !field->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty *>(field)->GetInner()->GetElementSize() != sizeof(UObject *))
        {
            return false;
        }
        const FName motion(L"ODanceSpecialAct1", FNAME_Add);
        for (auto *set : *field->ContainerPtrToValuePtr<TArray<UObject *>>(instance))
        {
            for (const auto &entry : *animations(set))
            {
                if (entry.name == motion && is_uobject_valid_fast(entry.asset))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool route(int controller, std::wstring_view key, int event, bool gamepad)
    {
        if (!gamepad || (key != L"Gamepad_LeftTrigger" && key != L"Gamepad_RightTrigger" &&
                         key != L"Gamepad_FaceButton_Right"))
        {
            return false;
        }
        return swim_input.route(controller, std::wstring(key), event, [&] {
            bool &held = key == L"Gamepad_LeftTrigger"    ? descend_held
                         : key == L"Gamepad_RightTrigger" ? ascend_held
                                                          : dash_held;
            if (event == 1)
            {
                held = false;
                return true;
            }
            auto *pawn = character_swap::current_hero();
            if (!ready(pawn) || !character_swap::owns_swap_pawn(pawn))
            {
                return false;
            }
            auto *state = object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (!is_uobject_valid_fast(state) ||
                state->GetClassPrivate()->GetName() != L"TresPlayerState_Swim")
            {
                return false;
            }
            auto *step = state->GetPropertyByNameInChain(L"m_nSubStep");
            if (!step || step->GetElementSize() != sizeof(int32_t))
            {
                return false;
            }
            const auto phase = *step->ContainerPtrToValuePtr<int32_t>(state);
            constexpr int surface_idle = 0;
            constexpr int surface_moving = 1;
            constexpr int underwater = 4;
            const bool surface = phase == surface_idle || phase == surface_moving;
            if (!surface && phase != underwater)
            {
                return false;
            }
            if (surface && key != L"Gamepad_LeftTrigger")
            {
                return false;
            }
            if (swim_input_pawn.Get() != pawn)
            {
                descend_held = ascend_held = dash_held = dive_requested = false;
            }
            swim_input_pawn = make_uobject_weak(pawn);
            held = true;
            dive_requested = surface && key == L"Gamepad_LeftTrigger";
            return true;
        });
    }

    namespace
    {
        void update_swim_input(UObject *pawn, UObject *state)
        {
            auto *controller = object(pawn, L"Controller");
            auto *stick_function =
                controller->GetFunctionByNameInChain(L"GetInputAnalogStickState");
            struct StickInput
            {
                uint8_t which_stick{};
                uint8_t padding[3]{};
                float right{}, forward{};
            } stick;
            static_assert(sizeof(stick) == 12);
            if (!is_uobject_valid_fast(stick_function) ||
                stick_function->GetParmsSize() != sizeof(stick))
            {
                throw std::runtime_error("character_swim_stick_layout");
            }
            for (const auto &[name, offset] :
                 std::array{std::pair{L"WhichStick", offsetof(StickInput, which_stick)},
                            std::pair{L"StickX", offsetof(StickInput, right)},
                            std::pair{L"StickY", offsetof(StickInput, forward)}})
            {
                auto *field = find_function_param_by_name(stick_function, name);
                if (!field || field->GetOffset_Internal() != offset ||
                    field->GetElementSize() != (offset == 0 ? 1 : sizeof(float)))
                {
                    throw std::runtime_error("character_swim_stick_layout");
                }
            }
            // Use the game's processed input, including its native dead zone.
            controller->ProcessEvent(stick_function, &stick);
            auto *camera = object(controller, L"PlayerCameraManager");
            auto *rotation =
                camera ? camera->GetFunctionByNameInChain(L"GetCameraRotation") : nullptr;
            if (!is_uobject_valid_fast(rotation) ||
                rotation->GetParmsSize() != sizeof(std::array<float, 3>))
            {
                throw std::runtime_error("character_swim_camera_layout");
            }
            std::array<float, 3> angles{};
            camera->ProcessEvent(rotation, angles.data());
            if (!std::isfinite(stick.right) || !std::isfinite(stick.forward) ||
                !std::isfinite(angles[0]) || !std::isfinite(angles[1]))
            {
                throw std::runtime_error("character_swim_input_range");
            }
            constexpr float radians_per_degree = 3.14159265358979323846f / 180.0f;
            constexpr float degrees_per_radian = 1.0f / radians_per_degree;
            constexpr int underwater_phase = 4;
            const auto phase = *reinterpret_cast<int32_t *>(reinterpret_cast<uint8_t *>(state) +
                                                            state_phase_offset);
            const float yaw = angles[1] * radians_per_degree;
            const float camera_pitch =
                phase == underwater_phase ? angles[0] * radians_per_degree : 0.0f;
            const float forward = stick.forward * std::cos(camera_pitch);
            const float x = forward * std::cos(yaw) - stick.right * std::sin(yaw);
            const float y = forward * std::sin(yaw) + stick.right * std::cos(yaw);
            const float z = stick.forward * std::sin(camera_pitch);
            const float horizontal = std::hypot(x, y);
            const float magnitude = std::hypot(stick.right, stick.forward);
            const float steering_right = magnitude > 0 ? stick.right / magnitude : 0.0f;
            const float steering_forward = magnitude > 0 ? stick.forward / magnitude : 0.0f;
            const float heading = magnitude > 0 ? std::atan2(y, x) * degrees_per_radian : angles[1];
            const float pitch =
                magnitude > 0 ? std::atan2(z, horizontal) * degrees_per_radian : 0.0f;
            const bool owned_buttons = swim_input_pawn.Get() == pawn;
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto *state_interface =
                reinterpret_cast<uint8_t *>(pawn) + character_native::layout::player_state_interface_offset;
            // Sora passes left-stick X/Y separately for native high-speed
            // steering. The swim state multiplies these normalized axes by
            // MySwimDivingHighSpeedMoveRotSpeedYaw/Pitch and the frame delta.
            // Passing camera angles here would cause excessive turning.
            const bool accepted = reinterpret_cast<RequestSwim>(base + request_swim_rva)(
                state_interface, heading, pitch, magnitude, steering_right, steering_forward,
                owned_buttons && dash_held, false, owned_buttons && descend_held,
                owned_buttons && dive_requested, owned_buttons && ascend_held);
            if (accepted && owned_buttons)
            {
                dive_requested = false;
            }
        }
    } // namespace

    bool prepare(UObject *pawn)
    {
        if (!character_swap::owns_swap_pawn(pawn))
        {
            return true;
        }
        apply_movement(pawn);
        for (const auto& binding : {std::pair{L"m_AthleticRoot", sora_athletic_root.Get()},
                                     std::pair{L"m_pAthleticHitEffect", sora_athletic_hit_effect.Get()}})
        {
            auto* property = pawn->GetPropertyByNameInChain(binding.first);
            if (!is_uobject_valid_fast(binding.second) || !property ||
                property->GetElementSize() != sizeof(UObject*) ||
                (property->GetClass().GetName() != L"ObjectProperty" &&
                 property->GetClass().GetName() != L"ClassProperty"))
            {
                throw std::runtime_error("character_airstep_effect_layout");
            }
            *property->ContainerPtrToValuePtr<UObject*>(pawn) = binding.second;
        }
        if (ready(pawn))
        {
            return true;
        }
        auto *instance = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
        auto *source = find_loaded_runtime_asset(player_animation_support::sora_animation_set_path);
        auto *caribbean_source = find_loaded_runtime_asset(caribbean_animation_set_path);
        auto *graveyard_source = find_loaded_runtime_asset(character_union_cross::animation_set_path);
        auto *gigas_source = find_loaded_runtime_asset(gigas_animation_set_path);
        if (!is_uobject_valid_fast(instance) || !is_uobject_valid_fast(source) ||
            !is_uobject_valid_fast(caribbean_source) || !is_uobject_valid_fast(graveyard_source) ||
            !is_uobject_valid_fast(gigas_source))
        {
            // Selection preloads general, underwater, Union Cross and Gigas motions.
            return false;
        }
        if (!player_animation_support::supported_skeleton_layout())
        {
            return true;
        }
        // The native hopping-point discovery is enabled by this particle
        // template. Sora/Aqua author it; Riku/Kairi/Roxas leave it null.
        // Keep their authored effects and let the ordinary native discovery
        // and overlap checks select the actual mast/navigation components.
        if (!object(pawn, L"m_pHopEffect"))
        {
            auto *effect = sora_hop_effect.Get();
            auto *property = pawn->GetPropertyByNameInChain(L"m_pHopEffect");
            if (!effect || !property || property->GetClass().GetName() != L"ObjectProperty" ||
                property->GetElementSize() != sizeof(UObject *))
            {
                throw std::runtime_error("character_hopping_effect_layout");
            }
            *property->ContainerPtrToValuePtr<UObject *>(pawn) = effect;
        }
        auto *sets_field = instance->GetPropertyByNameInChain(L"MyAnimSets");
        if (!sets_field || !sets_field->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty *>(sets_field)->GetInner()->GetElementSize() !=
                sizeof(UObject *))
        {
            throw std::runtime_error("character_traversal_animation_sets_layout");
        }
        auto *sets = sets_field->ContainerPtrToValuePtr<TArray<UObject *>>(instance);
        // Resolve the small required-name catalog once. A new pawn should scan
        // each animation set once, rather than rediscovering its reflected
        // array and walking the entire set again for every borrowed motion.
        static const auto required_indices = []
        {
            std::unordered_map<FName, size_t> indices;
            indices.reserve(animation_names.size());
            for (size_t index = 0; index < animation_names.size(); ++index)
            {
                indices.emplace(FName(animation_names[index], FNAME_Add), index);
            }
            return indices;
        }();
        std::array<bool, animation_names.size()> authored{};
        for (auto *set : *sets)
        {
            for (const auto &entry : *animations(set))
            {
                const auto found = required_indices.find(entry.name);
                if (found != required_indices.end() && is_uobject_valid_fast(entry.asset))
                {
                    authored[found->second] = true;
                }
            }
        }
        std::array<UObject *, animation_names.size()> borrowed{};
        for (auto *source_set : {source, caribbean_source, graveyard_source, gigas_source})
        {
            std::array<bool, animation_names.size()> seen{};
            for (const auto &entry : *animations(source_set))
            {
                const auto found = required_indices.find(entry.name);
                if (found == required_indices.end())
                {
                    continue;
                }
                const auto index = found->second;
                if (!seen[index] && !borrowed[index])
                {
                    borrowed[index] = entry.asset;
                }
                seen[index] = true;
            }
        }
        std::vector<AnimationEntry> missing;
        std::unordered_set<UObject *> adapted_skeletons;
        missing.reserve(animation_names.size());
        for (size_t index = 0; index < animation_names.size(); ++index)
        {
            if (authored[index])
            {
                continue;
            }
            auto *asset = borrowed[index];
            if (!is_uobject_valid_fast(asset))
            {
                throw std::runtime_error("character_traversal_animation_missing");
            }
            if (adapted_skeletons.insert(object(asset, L"Skeleton")).second)
            {
                player_animation_support::allow_animation_skeleton(instance, asset);
            }
            missing.push_back({FName(animation_names[index], FNAME_Add), asset});
        }
        if (!missing.empty())
        {
            FStaticConstructObjectParameters parameters(source->GetClassPrivate(), instance);
            parameters.SetFlags = RF_Transient;
            auto *private_set = UObjectGlobals::StaticConstructObject(parameters);
            if (!is_uobject_valid_fast(private_set))
            {
                throw std::runtime_error("character_traversal_animset_create");
            }
            auto *private_animations = animations(private_set);
            for (const auto &entry : missing)
            {
                private_animations->Add(entry);
            }
            sets->Add(private_set);
        }
        character_rails::prepare(pawn);
        prepared_pawn = make_uobject_weak(pawn);
        prepared_animation = make_uobject_weak(instance);
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Character traversal ready pawn={} borrowed_animations={}\n"),
            pawn->GetName(), missing.size());
        return true;
    }
} // namespace kh3ap::character_traversal
