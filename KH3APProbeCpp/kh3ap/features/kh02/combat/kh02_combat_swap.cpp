#include "kh3ap/features/player/characters/character_profile.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh02_combat_swap.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/characters/character_exploration.h"
#include "kh3ap/features/player/characters/character_camera.h"
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/diagnostics/profiling/engine_hitch_trace.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh02_attack_input_buffer.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh02_swing_hit_history.h"
#include "kh3ap/features/kh02/challenges/kh02_damage_event_decoder.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/NumericPropertyTypes.hpp>
#include <Unreal/NameTypes.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/diagnostics/character_swap_trace.h"
#include "kh3ap/app/enums.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include <nlohmann/json.hpp>
#include <deque>
#endif
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <cstring>
#include <utility>
#include <cmath>
#include <algorithm>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        using Clock = std::chrono::steady_clock;

        // Metadata survives pawn replacements, while every value is read from
        // the live receiver. Weak class serials invalidate unloaded layouts.
        thread_local ReflectedPropertyCache<128> movement_properties;

        UObject* swap_context_world()
        {
            if (auto* world = kh02_current_story_world())
            {
                return world;
            }
            return character_swap::enabled() ? kh02_current_world() : nullptr;
        }

        int attack_kind(int player)
        {
            if (is_standard_sora_player_id(player))
            {
                return player_state_ids::attack;
            }
            if (player == player_id_value(PlayerId::Aqua))
            {
                return player_state_ids::attack_aqua;
            }
            if (player == player_id_value(PlayerId::Riku) || player == player_id_value(PlayerId::RikuKH29))
            {
                return player_state_ids::attack_riku;
            }
            if (player == player_id_value(PlayerId::Roxas))
            {
                return player_state_ids::attack_roxas;
            }
            if (player == player_id_value(PlayerId::Kairi))
            {
                return player_state_ids::attack_kairi;
            }
            return -1;
        }

        constexpr auto combo_grace = std::chrono::milliseconds(750);
        constexpr size_t hud_selected_main_command_offset = 0x6f0;
        constexpr size_t hud_command_state_offset = 0xac8;
        constexpr int hud_main_command_state = 1;
        constexpr auto switch_timeout = std::chrono::seconds(2);
        constexpr int run_loop_phase = 0;
        constexpr int run_entry_phase = 2;
        constexpr int run_stance_blend_phase = 3;
        constexpr size_t run_walk_active_offset = 0x94;
        constexpr size_t run_walk_input_offset = 0x95;
        constexpr size_t run_walk_grace_offset = 0x98;
        constexpr uint8_t walking_movement_mode = 1;
        constexpr float maximum_first_movement_delta_seconds = 0.1f;
        constexpr auto first_movement_grace = std::chrono::milliseconds(250);
        constexpr float movement_intent_epsilon_squared = 0.0001f;
        constexpr float run_speed_epsilon_squared = 1.0f;
        constexpr float stick_continuity_epsilon_squared = 0.01f;

        float stick_distance_squared(const std::array<float, 2>& a, const std::array<float, 2>& b)
        {
            const float x = a[0] - b[0];
            const float y = a[1] - b[1];
            return x * x + y * y;
        }
        constexpr size_t combo_index_offset = 0x128;
        constexpr size_t combo_finisher_offset = 0x140;
        constexpr size_t combo_air_offset = 0x142;
        const uintptr_t sora_selection_rva = character_native::rva(character_native::Site::sora_selection);
        const uintptr_t aqua_selection_rva = character_native::rva(character_native::Site::aqua_selection);
        const uintptr_t attack_enter_rva = character_native::rva(character_native::Site::attack_enter);
        const uintptr_t attack_prepare_rva = character_native::rva(character_native::Site::attack_prepare);
        const uintptr_t queue_clear_rva = character_native::rva(character_native::Site::queue_clear);
        const uintptr_t queue_push_rva = character_native::rva(character_native::Site::queue_push);
        const uintptr_t queue_update_rva = character_native::rva(character_native::Site::queue_update);
        const uintptr_t air_jump_available_rva = character_native::rva(character_native::Site::air_jump_available);
        const uintptr_t set_air_jump_available_rva = character_native::rva(character_native::Site::set_air_jump_available);

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method) : Kh02ReflectedCall(owner, method, "kh02_combat_swap", 256) {}

            Call(const wchar_t* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_combat_swap", 256)
            {
            }
        };

        template <class T> auto field(void* object, size_t offset) -> T
        {
            T value{};
            SIZE_T copied{};
            if (!object ||
                !ReadProcessMemory(GetCurrentProcess(), static_cast<std::byte*>(object) + offset, &value, sizeof(value),
                                   &copied) ||
                copied != sizeof(value))
            {
                throw std::runtime_error("kh02_combat_swap_native_field");
            }
            return value;
        }

        auto object(UObject* owner, const wchar_t* name) -> UObject*
        {
            return movement_properties.read_object(owner, name);
        }

        auto player_id(UObject* pawn) -> int
        {
            auto* property = movement_properties.resolve(pawn, L"m_PlayerUniqueID");
            return property && property->GetElementSize() == 1 ? *property->ContainerPtrToValuePtr<uint8_t>(pawn) : -1;
        }

        auto state(UObject* pawn) -> UObject*
        {
            return object(object(pawn, L"MyStateComp"), L"m_pCurrentState");
        }

        auto state_id(UObject* current) -> int
        {
            auto* property = movement_properties.resolve(current, L"MyStateID");
            return property && property->GetElementSize() == 1 ? *property->ContainerPtrToValuePtr<uint8_t>(current)
                                                               : -1;
        }

        bool ordinary_state(int id)
        {
            // Idle, Run, Jump (including falling), supported normal attacks,
            // Guard, DodgeRoll and RiskDodge. Scripted/special states retain R2.
            return id == player_state_ids::idle || id == player_state_ids::run || id == player_state_ids::jump ||
                   id == player_state_ids::attack || id == player_state_ids::attack_aqua ||
                   id == player_state_ids::attack_riku || id == player_state_ids::attack_roxas ||
                   id == player_state_ids::attack_kairi || id == player_state_ids::guard ||
                   id == player_state_ids::dodge_roll || id == player_state_ids::risk_dodge;
        }

        bool attack_state(UObject* current, UObject* pawn)
        {
            if (!is_uobject_valid_fast(current) || object(current, L"MyCharPawn") != pawn)
            {
                return false;
            }
            const auto name = current->GetClassPrivate()->GetName();
            auto* target = current->GetPropertyByNameInChain(L"m_pTargetPawn");
            return (name == L"TresPlayerState_Attack" || name == L"TresPlayerState_AttackAQ" ||
                    name == L"TresPlayerState_AttackRI" || name == L"TresPlayerState_AttackKA" ||
                    name == L"TresPlayerState_AttackRO") &&
                   target && target->GetOffset_Internal() == 0x110;
        }

        auto vector(UObject* owner, const wchar_t* method) -> std::array<float, 3>
        {
            Call call(owner, method);
            call.run();
            return call.result_vector();
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

        bool has_motion(UObject* instance, FName name)
        {
            auto* sets = is_uobject_valid_fast(instance) ? instance->GetPropertyByNameInChain(L"MyAnimSets") : nullptr;
            if (!sets || !sets->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(sets)->GetInner()->GetElementSize() != sizeof(UObject*))
            {
                return false;
            }
            using player_animation_support::AnimationEntry;
            for (auto* set : *sets->ContainerPtrToValuePtr<TArray<UObject*>>(instance))
            {
                auto* entries = is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
                if (!entries || !entries->IsA<FArrayProperty>() ||
                    static_cast<FArrayProperty*>(entries)->GetInner()->GetElementSize() != sizeof(AnimationEntry))
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

        FName resolve_run_motion(UObject* instance, FName source)
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

        bool state_boolean(UObject* owner, const wchar_t* name)
        {
            auto* property = owner->GetPropertyByNameInChain(name);
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_swing_boolean_layout");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValue(
                property->ContainerPtrToValuePtr<void>(owner));
        }

        auto animation_value(UObject* anim, const wchar_t* method) -> float
        {
            Call call(anim, method);
            call.name(L"InSlotName", L"FullBody");
            call.run();
            auto* returned = call.param(L"ReturnValue");
            if (!returned || returned->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("kh02_swap_animation_value");
            }
            return *returned->ContainerPtrToValuePtr<float>(call.parameters.data());
        }

        auto camera_manager(UObject* pawn) -> UObject*
        {
            return object(object(pawn, L"Controller"), L"PlayerCameraManager");
        }

        auto local_player_pawn(UObject* world) -> UObject*
        {
            Call controller(L"/Script/Engine.Default__GameplayStatics", L"GetPlayerController");
            controller.object(L"WorldContextObject", world);
            controller.integer(L"PlayerIndex", 0);
            controller.run();
            auto* owner = controller.result();
            auto* pawn = object(owner, L"Pawn");
            return object(pawn, L"Controller") == owner ? pawn : nullptr;
        }

        auto movement_stick(UObject* pawn) -> std::array<float, 2>
        {
            return runtime_controller_stick_state(object(pawn, L"Controller"));
        }

        struct CameraDistance
        {
            float current{}, battle{}, normal{};
        };

        struct CameraOrbit
        {
            std::array<float, 3> target_rotation{}, angular_delta{}, pivot{}, target_delta{}, last_target{};
            float recenter_wait{}, input_wait{}, reset_blend{};
            uint8_t reset_rotation{};
            uint8_t rotation_lerp{};
        };

        auto camera_orbit(UObject* pawn) -> CameraOrbit
        {
            auto* camera = object(camera_manager(pawn), L"m_CameraNormal1st");
            return {field<std::array<float, 3>>(camera, 0x898),
                    field<std::array<float, 3>>(camera, 0x8a4),
                    field<std::array<float, 3>>(camera, 0x8b0),
                    field<std::array<float, 3>>(camera, 0x8c0),
                    field<std::array<float, 3>>(camera, 0x8cc),
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
            for (const auto& [name, offset] : {std::pair{L"m_CurrentDistance", 0x428}, std::pair{L"m_Distance", 0x800},
                                               std::pair{L"m_NonBattleDistance", 0x900}})
            {
                auto* property = camera->GetPropertyByNameInChain(name);
                if (!property || property->GetOffset_Internal() != offset ||
                    property->GetElementSize() != sizeof(float))
                {
                    throw std::runtime_error("kh02_combat_swap_distance_layout");
                }
            }
            return {field<float>(camera, 0x428), field<float>(camera, 0x800), field<float>(camera, 0x900)};
        }

        struct CameraView
        {
            std::array<float, 3> location;
            std::array<float, 3> rotation;
            float fov;
        };

        static_assert(sizeof(CameraView) == 28);

        auto camera_timestamp(UObject* manager) -> float*
        {
            auto* cache = is_uobject_valid_fast(manager) ? manager->GetPropertyByNameInChain(L"CameraCache") : nullptr;
            auto* type = cache && cache->IsA<FStructProperty>() ? static_cast<FStructProperty*>(cache)->GetStruct() : nullptr;
            auto* timestamp = type ? find_struct_field_by_name(type, L"Timestamp") : nullptr;
            if (!timestamp || !timestamp->IsA<FFloatProperty>() || timestamp->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("character_swap_camera_timestamp_layout");
            }
            return reinterpret_cast<float*>(cache->ContainerPtrToValuePtr<std::byte>(manager) + timestamp->GetOffset_Internal());
        }

        auto camera_pov(UObject* manager, const wchar_t* name) -> std::byte*
        {
            auto* entry = is_uobject_valid_fast(manager) ? manager->GetPropertyByNameInChain(name) : nullptr;
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
            if (!view_type || view_type->GetName() != L"MinimalViewInfo" || !fov || fov->GetOffset_Internal() != 24 ||
                fov->GetElementSize() != sizeof(float) || pov->GetElementSize() < sizeof(CameraView))
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
            auto* distance =
                is_uobject_valid_fast(camera) ? camera->GetPropertyByNameInChain(L"m_CurrentDistance") : nullptr;
            if (!is_uobject_valid_fast(controller) || object(controller, L"Pawn") != pawn ||
                !is_uobject_valid_fast(root) || !distance || distance->GetOffset_Internal() != 0x428)
            {
                throw std::runtime_error("kh02_combat_swap_camera_context");
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto camera_vtable = field<uintptr_t>(camera, 0);
            if (field<uintptr_t>(reinterpret_cast<void*>(camera_vtable), 0x7a0) != base + character_native::rva(character_native::Site::camera_target_init))
            {
                throw std::runtime_error("kh02_combat_swap_camera_profile");
            }
            Call control(controller, L"SetControlRotation");
            control.vector(L"NewRotation", control_rotation[0], control_rotation[1], control_rotation[2]);
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
            // Preserve both the current interpolation value and its authored
            // targets, otherwise the next update zooms to the new hero's default.
            std::memcpy(bytes + 0x428, &zoom.current, sizeof(float));
            std::memcpy(bytes + 0x8bc, &zoom.current, sizeof(float));
            std::memcpy(bytes + 0x800, &zoom.battle, sizeof(float));
            std::memcpy(bytes + 0x900, &zoom.normal, sizeof(float));
            // Possession has already populated these with the default view.
            // Rendering and view-target blending can consume them before the
            // field camera updates. Seed their scalar POV immediately, without
            // copying target pointers or post-process arrays.
            for (const auto* name : {L"CameraCache", L"LastFrameCameraCache", L"ViewTarget", L"PendingViewTarget"})
            {
                std::memcpy(camera_pov(manager, name), &rendered_view, sizeof(rendered_view));
            }
            // GetPlayerViewPoint ignores CameraCache while Timestamp is zero.
            // Carry the same-world source view's time too, so the first input
            // pass uses this view instead of the new controller's default yaw.
            *camera_timestamp(manager) = rendered_timestamp;
        }
    } // namespace

    struct Kh02CombatSwap::Impl
    {
        using Select = int (*)(UObject*);
        inline static Impl* active{};
        Player player;
        Change change;
        std::unique_ptr<PLH::x64Detour> sora_hook, aqua_hook, run_hook, camera_hook, attack_hook, prepare_hook;
        std::array<std::unique_ptr<PLH::x64Detour>, 3> alternate_hooks;
        std::array<uint64_t, 3> original_alternates{};
        uint64_t original_sora{}, original_aqua{}, original_run{}, original_camera{}, original_attack{},
            original_prepare{};
        bool attempted{}, installed{}, queued{}, dispatched{}, input_seeded{};
        bool waiting_for_preload{};
        FWeakObjectPtr outgoing, request_world, incoming, positioned;
        FWeakObjectPtr first_movement_pawn;
        bool first_movement_pending{};
        std::array<float, 3> first_movement_location{};
        Clock::time_point first_movement_deadline{};
        int target{-1}, next_index{-1};
        bool air{}, falling{};
        bool air_jump_available{};
        uint32_t floating_mode{};
        std::array<float, 3> location{}, rotation{}, velocity{};
        std::array<float, 3> control_rotation{};
        RuntimeControllerStickState stick_input{}, look_input{};
        std::array<float, 3> movement_input{}, last_movement_input{}, acceleration{};
        float analog_modifier{}, control_modifier{};
        float movement_max_speed{}, movement_max_acceleration{};
        bool run_handoff{}, run_loop{};
        uint8_t run_walk_active{}, run_walk_input{};
        float run_walk_grace{};
        float run_heading{}, run_elapsed{}, run_phase{}, run_play_rate{1.0f};
        int run_step{};
        uint32_t run_flags{};
        FName run_animation{};
        bool swing_pending{}, resuming_swing{};
        int swing_index{-1};
        float swing_phase{}, swing_progress_rate{};
        bool swing_air{};
        bool swing_input_ready{}, swing_change_ready{};
        FWeakObjectPtr swing_target, swing_homing, swing_owner, swing_state;
        Kh02SwingHitHistory swing_hits;
        Kh02AttackInputBuffer attack_buffer;
        RuntimeOptionsInputOwnership attack_input;
        FWeakObjectPtr attack_press_controller;
        Clock::time_point attack_input_until{}, attack_release_at{};
        bool attack_press_held{}, replaying_attack{};
        DWORD game_thread{};
        Clock::time_point run_until{};
        CameraView rendered_view{};
        float rendered_timestamp{};
        CameraDistance zoom{};
        CameraOrbit orbit{};
        FWeakObjectPtr transition_camera;
        float camera_frame_delta{1.0f / 60.0f};
        int camera_transition_frames{};
        Clock::time_point requested_at{}, combo_until{};
        std::string error;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t swaps{};
        uint64_t first_movement_updates{};
        std::deque<nlohmann::json> selections;
        FWeakObjectPtr test_world, test_controller;
        Clock::time_point test_until{}, test_next{}, test_release{};
        int test_phase{}, test_target{}, test_expected_index{};
        int test_input_mode{}, test_origin{};
        float test_swing_phase{0.35f};
        bool test_air{}, test_expire{}, test_held{}, test_jump{};
        bool test_camera{}, test_movement{}, test_rotate{}, test_menu{};
        bool test_locomotion{};
        bool test_camera_relative{};
        bool test_release_at_swap{};
        bool test_capture_gait{};
        std::map<int, std::vector<std::wstring>> test_gait_bones;
        int test_swaps_remaining{};
        float test_strength{};
        Clock::time_point test_started{};
        std::wstring test_action;
        std::string test_status = "idle";
        nlohmann::json test_result;
        bool camera_control_ready{}, camera_ui_current{};
        nlohmann::json camera_deltas = nlohmann::json::array();
        nlohmann::json swing_resume;
        uint64_t duplicate_hits_blocked{};
        uint64_t buffered_presses{}, replayed_presses{}, accepted_presses{};
#endif

        Impl(Player get_player, Change change_player) : player(std::move(get_player)), change(std::move(change_player))
        {
        }

        ~Impl()
        {
            release_attack_press();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            release_test_input();
#endif
            if (sora_hook && sora_hook->isHooked())
            {
                sora_hook->unHook();
            }
            if (aqua_hook && aqua_hook->isHooked())
            {
                aqua_hook->unHook();
            }
            if (run_hook && run_hook->isHooked())
            {
                run_hook->unHook();
            }
            if (camera_hook && camera_hook->isHooked())
            {
                camera_hook->unHook();
            }
            if (attack_hook && attack_hook->isHooked())
            {
                attack_hook->unHook();
            }
            if (prepare_hook && prepare_hook->isHooked())
            {
                prepare_hook->unHook();
            }
            for (auto& hook : alternate_hooks)
            {
                if (hook && hook->isHooked())
                {
                    hook->unHook();
                }
            }
            if (active == this)
            {
                active = nullptr;
            }
        }

        bool install()
        {
            if (attempted)
            {
                return installed;
            }
            attempted = true;
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!character_native::verify_sites({character_native::Site::sora_selection, character_native::Site::aqua_selection, character_native::Site::attack_enter, character_native::Site::attack_prepare, character_native::Site::queue_clear, character_native::Site::queue_push, character_native::Site::queue_update, character_native::Site::run_enter, character_native::Site::camera_update, character_native::Site::riku_selection, character_native::Site::kairi_selection, character_native::Site::roxas_selection, character_native::Site::camera_target_init, character_native::Site::air_jump_available, character_native::Site::set_air_jump_available, character_native::Site::skeletal_tick_animation, character_native::Site::skeletal_refresh_bones}))
            {
                error = "kh02_combat_swap_build_profile";
                return false;
            }
            const std::array<uintptr_t, 3> selectors{character_native::rva(character_native::Site::riku_selection), character_native::rva(character_native::Site::kairi_selection), character_native::rva(character_native::Site::roxas_selection)};
            active = this;
            game_thread = GetCurrentThreadId();
            sora_hook = std::make_unique<PLH::x64Detour>(base + sora_selection_rva,
                                                         reinterpret_cast<uintptr_t>(&select_sora), &original_sora);
            aqua_hook = std::make_unique<PLH::x64Detour>(base + aqua_selection_rva,
                                                         reinterpret_cast<uintptr_t>(&select_aqua), &original_aqua);
            run_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(character_native::Site::run_enter), reinterpret_cast<uintptr_t>(&enter_run),
                                                        &original_run);
            camera_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(character_native::Site::camera_update), reinterpret_cast<uintptr_t>(&update_camera),
                                                           &original_camera);
            attack_hook = std::make_unique<PLH::x64Detour>(
                base + attack_enter_rva, reinterpret_cast<uintptr_t>(&enter_attack), &original_attack);
            prepare_hook = std::make_unique<PLH::x64Detour>(
                base + attack_prepare_rva, reinterpret_cast<uintptr_t>(&prepare_attack), &original_prepare);
            const std::array<Select, 3> callbacks{&select_riku, &select_kairi, &select_roxas};
            for (size_t index = 0; index < selectors.size(); ++index)
            {
                alternate_hooks[index] = std::make_unique<PLH::x64Detour>(base + selectors[index],
                                                                          reinterpret_cast<uintptr_t>(callbacks[index]),
                                                                          &original_alternates[index]);
            }
            installed = sora_hook->hook() && original_sora && aqua_hook->hook() && original_aqua && run_hook->hook() &&
                        original_run && camera_hook->hook() && original_camera && attack_hook->hook() &&
                        original_attack && prepare_hook->hook() && original_prepare;
            for (size_t index = 0; index < alternate_hooks.size() && installed; ++index)
            {
                installed = alternate_hooks[index]->hook() && original_alternates[index];
            }
            if (!installed)
            {
                if (sora_hook->isHooked())
                {
                    sora_hook->unHook();
                }
                if (aqua_hook->isHooked())
                {
                    aqua_hook->unHook();
                }
                if (run_hook->isHooked())
                {
                    run_hook->unHook();
                }
                if (camera_hook->isHooked())
                {
                    camera_hook->unHook();
                }
                if (attack_hook->isHooked())
                {
                    attack_hook->unHook();
                }
                if (prepare_hook->isHooked())
                {
                    prepare_hook->unHook();
                }
                for (auto& hook : alternate_hooks)
                {
                    if (hook && hook->isHooked())
                    {
                        hook->unHook();
                    }
                }
                active = nullptr;
                error = "kh02_combat_swap_selection_hook";
            }
            return installed;
        }

        static bool available(UObject* pawn)
        {
            const int id = player_id(pawn);
            if (!kh02_current_story_world())
            {
                return character_swap::swap_available(pawn) &&
                       (ordinary_state(state_id(state(pawn))) || state_id(state(pawn)) == player_state_ids::glide ||
                        state_id(state(pawn)) == player_state_ids::floating);
            }
            if ((id != player_id_value(PlayerId::Sora) && id != player_id_value(PlayerId::Aqua)) ||
                !Kh02Story::native_save_ready(pawn) || kh02_actor_world(pawn) != swap_context_world() ||
                !runtime_sora_control_ready(pawn) || Kh02Story::challenges_open() ||
                !ordinary_state(state_id(state(pawn))))
            {
                return false;
            }
            Call paused(L"/Script/Engine.Default__GameplayStatics", L"IsGamePaused");
            paused.object(L"WorldContextObject", pawn);
            paused.run();
            return !paused.boolean_result();
        }

        static int select_riku(UObject* current)
        {
            return active->select(current, active->original_alternates[0]);
        }

        static int select_kairi(UObject* current)
        {
            return active->select(current, active->original_alternates[1]);
        }

        static int select_roxas(UObject* current)
        {
            return active->select(current, active->original_alternates[2]);
        }

        static int select_sora(UObject* current)
        {
            return active->select(current, active->original_sora);
        }

        static int select_aqua(UObject* current)
        {
            return active->select(current, active->original_aqua);
        }

        static void update_camera(UObject* camera, float delta)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const character_swap_trace::NativeScope trace{"camera_update"};
#endif
            auto* self = active;
            float applied_delta = delta;
            if (self->camera_transition_frames > 0 && self->transition_camera.Get() == camera &&
                kh02_actor_world(camera) == self->request_world.Get())
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
                *orbit_flags = static_cast<uint8_t>((*orbit_flags & ~0x10u) | self->orbit.rotation_lerp);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                self->camera_deltas.push_back({{"native", delta},
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

        void restore_movement(UObject* pawn)
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
            const float pending_size = movement_input[0] * movement_input[0] + movement_input[1] * movement_input[1];
            set_property(pawn, L"ControlInputVector", pending_size > movement_intent_epsilon_squared ? movement_input : last_movement_input);
            set_property(pawn, L"LastControlInputVector", last_movement_input);
        }

        static void enter_run(UObject* current)
        {
            auto* self = active;
            reinterpret_cast<void (*)(UObject*)>(self->original_run)(current);
            try
            {
                auto* pawn = object(current, L"MyCharPawn");
                if (!self->run_handoff || Clock::now() > self->run_until || !is_uobject_valid_fast(pawn) ||
                    player_id(pawn) != self->target || pawn == self->outgoing.Get() ||
                    kh02_actor_world(pawn) != self->request_world.Get() ||
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

        static void prepare_attack(UObject* current)
        {
            auto* self = active;
            reinterpret_cast<void (*)(UObject*)>(self->original_prepare)(current);
            if (!self->resuming_swing)
            {
                return;
            }
            try
            {
                auto* pawn = object(current, L"MyCharPawn");
                if (player_id(pawn) == self->target && kh02_actor_world(pawn) == self->request_world.Get() &&
                    attack_state(current, pawn))
                {
                    // Keep an ongoing air swing even if this selector's ground
                    // proximity test sees a surface during the handoff.
                    *reinterpret_cast<uint8_t*>(reinterpret_cast<std::byte*>(current) + combo_air_offset) =
                        self->swing_air;
                }
            }
            catch (const std::exception& failure)
            {
                self->error = failure.what();
            }
        }

        static void enter_attack(UObject* current)
        {
            auto* self = active;
            auto* pawn = object(current, L"MyCharPawn");
            const bool resume = self->resuming_swing && player_id(pawn) == self->target &&
                                kh02_actor_world(pawn) == self->request_world.Get() && attack_state(current, pawn);
            reinterpret_cast<void (*)(UObject*)>(self->original_attack)(current);
            if (!resume)
            {
                return;
            }
            try
            {
                auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
                const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
                if (length <= 0.0f)
                {
                    throw std::runtime_error("kh02_swing_resume_animation_unavailable");
                }
                Call rate(anim, L"SQEX_AnimSetCurrentPlayRate");
                rate.name(L"InSlotName", L"FullBody");
                rate.number(L"InPlayRate", self->swing_progress_rate * length);
                rate.run();
                Call seek(anim, L"SQEX_AnimSetCurrentTime");
                seek.name(L"InSlotName", L"FullBody");
                seek.number(L"InTime", self->swing_phase * length);
                seek.run();
                // Seeking skips earlier animation notifications. Carry the
                // outgoing swing's already-open input/cancel gates through the
                // incoming animation's normal notify handlers.
                if (self->swing_input_ready)
                {
                    Call input(anim, L"NotifyEnableInput");
                    input.run();
                }
                if (self->swing_change_ready)
                {
                    Call cancel(anim, L"NotifyEnableChange");
                    cancel.run();
                }
                self->swing_pending = false;
                self->next_index =
                    field<uint8_t>(current, combo_finisher_offset) ? -1 : field<int>(current, combo_index_offset) + 1;
                self->air = field<uint8_t>(current, combo_air_offset) != 0;
                self->incoming = make_uobject_weak(pawn);
                const float play_rate = animation_value(anim, L"SQEX_AnimGetCurrentPlayRate");
                const float remaining = (1.0f - self->swing_phase) * length / std::max(play_rate, 0.01f);
                self->combo_until =
                    Clock::now() + combo_grace +
                    std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(remaining));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                self->swing_resume = {{"player", player_id(pawn)},
                                      {"index", field<int>(current, combo_index_offset)},
                                      {"source_phase", self->swing_phase},
                                      {"time", animation_value(anim, L"SQEX_AnimGetCurrentTime")},
                                      {"length", length},
                                      {"source_progress_rate", self->swing_progress_rate},
                                      {"play_rate", play_rate},
                                      {"air", field<uint8_t>(current, combo_air_offset) != 0}};
#endif
            }
            catch (const std::exception& failure)
            {
                self->error = failure.what();
            }
        }

        void resume_swing(UObject* pawn)
        {
            if (!swing_pending)
            {
                return;
            }
            auto* component = object(pawn, L"MyStateComp");
            if (!is_uobject_valid_fast(component) ||
                component->GetClassPrivate()->GetName() != L"TresStateQueueComponent")
            {
                throw std::runtime_error("kh02_swing_state_queue");
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            resuming_swing = true;
            try
            {
                // Use the owning native queue's clear/push/update lifecycle.
                // The new hero constructs its own attack, targets and animation.
                reinterpret_cast<void (*)(UObject*)>(base + queue_clear_rva)(component);
                auto* attack = reinterpret_cast<UObject* (*)(UObject*, int)>(base + queue_push_rva)(
                    component, attack_kind(target));
                if (!attack_state(attack, pawn))
                {
                    throw std::runtime_error("kh02_swing_attack_creation");
                }
                set_property(attack, L"m_pTargetPawn", swing_target.Get());
                set_property(attack, L"m_HomingTargetComponent", swing_homing.Get());
                reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component, 0.0f);
            }
            catch (...)
            {
                resuming_swing = false;
                throw;
            }
            resuming_swing = false;
        }

        void resume_run(UObject* pawn)
        {
            if (!run_handoff || swing_pending)
            {
                return;
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
            reinterpret_cast<void (*)(UObject*)>(base + queue_clear_rva)(component);
            auto* run = reinterpret_cast<UObject* (*)(UObject*, int)>(base + queue_push_rva)(component, player_state_ids::run);
            if (!is_uobject_valid_fast(run) || object(run, L"MyCharPawn") != pawn ||
                run->GetClassPrivate()->GetName() != L"TresPlayerState_Run")
            {
                throw std::runtime_error("character_run_state_creation");
            }
            reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component, 0.0f);
            if (run_handoff || state(pawn) != run)
            {
                return;
            }
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
                return;
            }
            Call phase(anim, L"SQEX_AnimSetCurrentTime");
            phase.name(L"InSlotName", L"FullBody");
            phase.number(L"InTime", run_phase * length);
            phase.run();
            // This transaction runs after the engine's mesh tick. Evaluate the
            // incoming pose now, at zero delta, so its parked bone transforms
            // cannot be submitted for the first visible frame. Passing no tick
            // function makes the normal refresh finish synchronously.
            auto* mesh = object(pawn, L"MyMesh");
            const auto refresh = base + character_native::rva(character_native::Site::skeletal_refresh_bones);
            constexpr size_t refresh_bones_vtable_offset = 0xac0;
            if (!is_uobject_valid_fast(mesh) ||
                field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(mesh, 0)), refresh_bones_vtable_offset) != refresh)
            {
                throw std::runtime_error("character_run_pose_mesh_layout");
            }
            using TickAnimation = void (*)(UObject*, float, bool);
            using RefreshBones = void (*)(UObject*, void*);
            const auto tick_animation = base + character_native::rva(character_native::Site::skeletal_tick_animation);
            reinterpret_cast<TickAnimation>(tick_animation)(mesh, 0.0f, false);
            reinterpret_cast<RefreshBones>(refresh)(mesh, nullptr);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (test_locomotion && test_phase != 0)
            {
                test_result["run_resumes"].push_back({
                    {"player", player_id(pawn)},
                    {"source_phase", run_phase},
                    {"time", animation_value(anim, L"SQEX_AnimGetCurrentTime")},
                    {"length", length},
                    {"motion", to_utf8(field<FName>(run, 0xa0).ToString())}});
            }
#endif
        }

        int select(UObject* current, uint64_t original)
        {
            auto* pawn = object(current, L"MyCharPawn");
            bool applied = false;
            try
            {
                if (resuming_swing && player_id(pawn) == target && kh02_actor_world(pawn) == request_world.Get())
                {
                    *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(current) + combo_index_offset) = swing_index;
                    *reinterpret_cast<uint8_t*>(reinterpret_cast<std::byte*>(current) + combo_air_offset) = swing_air;
                    swing_hits.resume();
                    applied = true;
                }
                else if (const auto* intent = attack_buffer.front();
                         intent && intent->attempted && pawn == local_player_pawn(request_world.Get()) &&
                         attack_state(current, pawn) && kh02_actor_world(pawn) == request_world.Get())
                {
                    if (field<int>(current, combo_index_offset) == 0 &&
                        (field<uint8_t>(current, combo_air_offset) != 0) == intent->air)
                    {
                        *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(current) + combo_index_offset) =
                            intent->index;
                        applied = true;
                    }
                    next_index = -1;
                    incoming = {};
                }
                else if (next_index >= 0 && incoming.Get() == pawn && Clock::now() <= combo_until &&
                         pawn == local_player_pawn(request_world.Get()) &&
                         kh02_actor_world(pawn) == request_world.Get() && attack_state(current, pawn))
                {
                    if (field<int>(current, combo_index_offset) == 0 &&
                        (field<uint8_t>(current, combo_air_offset) != 0) == air)
                    {
                        *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(current) + combo_index_offset) =
                            next_index;
                        applied = true;
                    }
                    // Consume only this first native attack selection. A later
                    // attack/finisher must advance under its own native rules.
                    next_index = -1;
                    incoming = {};
                }
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                next_index = -1;
                incoming = {};
            }
            const int selected = reinterpret_cast<Select>(original)(current);
            [[maybe_unused]] uint64_t input_serial{};
            if (kh02_actor_world(pawn) == swap_context_world() && attack_state(current, pawn) &&
                local_player_pawn(swap_context_world()) == pawn)
            {
                if (!resuming_swing)
                {
                    swing_hits.begin();
                    if (const auto* intent = attack_buffer.front();
                        intent && (intent->attempted || intent->index == field<int>(current, combo_index_offset)))
                    {
                        input_serial = intent->serial;
                        attack_buffer.accepted();
                        attack_input_until = {};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                        ++accepted_presses;
#endif
                    }
                }
                swing_owner = make_uobject_weak(pawn);
                swing_state = make_uobject_weak(current);
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (kh02_actor_world(pawn) == swap_context_world() && attack_state(current, pawn) &&
                local_player_pawn(swap_context_world()) == pawn)
            {
                selections.push_back({{"player", player_id(pawn)},
                                      {"index", field<int>(current, combo_index_offset)},
                                      {"attack", selected},
                                      {"air", field<uint8_t>(current, combo_air_offset) != 0},
                                      {"finisher", field<uint8_t>(current, combo_finisher_offset) != 0},
                                      {"resumed_swing", resuming_swing},
                                      {"handoff", applied}});
                selections.back()["input_serial"] = input_serial;
                if (selections.size() > 32)
                {
                    selections.pop_front();
                }
            }
#endif
            return selected;
        }

        std::optional<bool> attack_command_selected(UObject* pawn)
        {
            auto* manager = object(object(kh02_current_viewport(), L"GameInstance"), L"m_UIManager");
            auto* controller = object(manager, L"m_pPlayerController");
            if (!is_uobject_valid_fast(controller) ||
                (controller != object(pawn, L"Controller") &&
                 (!queued || controller != object(outgoing.Get(), L"Controller"))))
            {
                return std::nullopt;
            }
            auto* active_hud = object(manager, L"m_ActiveHUD");
            auto* hud = object(object(active_hud, L"TresCockpit"), L"m_pHudCommand");
            if (!is_uobject_valid_fast(hud) || hud->GetClassPrivate()->GetName() != L"TresUIP_HudCommand" ||
                kh02_actor_world(active_hud) != kh02_actor_world(pawn) || object(hud, L"m_pUIManager") != manager)
            {
                return std::nullopt;
            }
            // The retail HUD owns the selection, even while the pawn is Idle.
            // Validate its reflected command-array layout before reading the
            // adjacent native main-selection/state fields used by its reset.
            auto* commands = movement_properties.resolve(hud, L"m_mainCmdInfoAry");
            if (!commands || !commands->IsA<FStructProperty>() || commands->GetOffset_Internal() != 0x1d0 ||
                commands->GetElementSize() != 32 || commands->GetArrayDim() != 4)
            {
                return std::nullopt;
            }
            const int command_state = field<int>(hud, hud_command_state_offset);
            if (command_state == 0)
            {
                if (field<int>(hud, hud_selected_main_command_offset) != 0)
                {
                    return false;
                }
                // A resumed native swing already establishes Attack ownership
                // while its new HUD initializes. Preserve that combo's input.
                if (attack_state(state(pawn), pawn))
                {
                    return true;
                }
                return std::nullopt;
            }
            return command_state == hud_main_command_state && field<int>(hud, hud_selected_main_command_offset) == 0;
        }

        bool attack_key(int controller, std::wstring_view key, int event)
        {
            if (replaying_attack || (key != L"Gamepad_FaceButton_Bottom" && key != L"LeftMouseButton"))
            {
                return false;
            }
            return attack_input.route(
                controller, std::wstring(key), event,
                [&]
                {
                    if (event != 0)
                    {
                        return true;
                    }
                    const auto now = Clock::now();
                    if ((!queued && now > attack_input_until && !attack_buffer.front()) ||
                        request_world.Get() != swap_context_world() || Kh02Story::challenges_open())
                    {
                        return false;
                    }
                    auto* pawn = local_player_pawn(request_world.Get());
                    if (!is_uobject_valid_fast(pawn) || attack_kind(player_id(pawn)) < 0)
                    {
                        return false;
                    }
                    if (!attack_command_selected(pawn).value_or(queued))
                    {
                        attack_buffer.clear();
                        return false;
                    }
                    Call paused(L"/Script/Engine.Default__GameplayStatics", L"IsGamePaused");
                    paused.object(L"WorldContextObject", pawn);
                    paused.run();
                    if (paused.boolean_result())
                    {
                        return false;
                    }
                    auto* current = state(pawn);
                    const int state_kind = state_id(current);
                    if (state_kind != player_state_ids::idle && state_kind != player_state_ids::run &&
                        state_kind != player_state_ids::jump && !attack_state(current, pawn))
                    {
                        return false;
                    }
                    int wanted = std::max(next_index, 0);
                    bool wanted_air = air;
                    if (attack_state(current, pawn))
                    {
                        wanted = field<uint8_t>(current, combo_finisher_offset)
                                     ? 0
                                     : field<int>(current, combo_index_offset) + 1;
                        wanted_air = field<uint8_t>(current, combo_air_offset) != 0;
                    }
                    const auto deadline =
                        queued ? now + switch_timeout + combo_grace : std::max(combo_until, now + combo_grace);
                    if (attack_buffer.push(wanted, wanted_air, deadline))
                    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                        ++buffered_presses;
#endif
                    }
                    // A full bounded buffer still owns this press: passing it to
                    // native input as well would duplicate an already queued hit.
                    return true;
                });
        }

        void release_attack_press()
        {
            if (!attack_press_held)
            {
                return;
            }
            replaying_attack = true;
            try
            {
                if (auto* controller = attack_press_controller.Get();
                    is_uobject_valid_fast(controller) &&
                    object(object(controller, L"Player"), L"PlayerController") == controller)
                {
                    runtime_controller_action_input(controller, L"attack", false);
                }
            }
            catch (...)
            {
                replaying_attack = false;
                attack_press_held = false;
                attack_press_controller = {};
                error = "kh02_buffered_attack_release";
                return;
            }
            replaying_attack = false;
            attack_press_held = false;
            attack_press_controller = {};
        }

        void tick_attack_buffer(Clock::time_point now)
        {
            attack_buffer.expire(now);
            if (!attack_buffer.front() || queued)
            {
                return;
            }
            if (request_world.Get() != swap_context_world())
            {
                attack_buffer.clear();
                return;
            }
            auto* pawn = local_player_pawn(request_world.Get());
            auto* current = state(pawn);
            const int kind = state_id(current);
            if (!available(pawn) || (kind != player_state_ids::idle && kind != player_state_ids::run &&
                                    kind != player_state_ids::jump && !attack_state(current, pawn)))
            {
                attack_buffer.clear();
                return;
            }
            const auto attack_selected = attack_command_selected(pawn);
            if (!attack_selected)
            {
                // Keep only this unfinished, expiring intent until the new HUD
                // publishes its selection. Never replay against unknown UI.
                return;
            }
            if (!*attack_selected)
            {
                attack_buffer.clear();
                return;
            }
            if (attack_press_held)
            {
                return;
            }
            const auto intent = *attack_buffer.front();
            if (intent.attempted && (now - intent.attempted_at < std::chrono::milliseconds(30) ||
                                     (attack_state(current, pawn) && (field<uint32_t>(current, 0x234) & 0x100u))))
            {
                return;
            }
            if (attack_state(current, pawn))
            {
                if (field<uint8_t>(current, combo_finisher_offset) && intent.index == 0)
                {
                    return;
                }
                // Accept the press now; the native attack state still decides
                // when its next motion can start. Do not open its cancel gate.
                auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
                Call input(anim, L"NotifyEnableInput");
                input.run();
            }
            auto* controller = object(pawn, L"Controller");
            attack_buffer.attempted(now);
            attack_press_controller = make_uobject_weak(controller);
            attack_press_held = true;
            attack_release_at = now + std::chrono::milliseconds(1);
            replaying_attack = true;
            try
            {
                runtime_controller_action_input(controller, L"attack", true);
            }
            catch (...)
            {
                replaying_attack = false;
                release_attack_press();
                throw;
            }
            replaying_attack = false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++replayed_presses;
#endif
        }

        void capture_frame(UObject* pawn)
        {
            // Doubleflight belongs to this airborne journey, not to the pawn
            // being created or woken by the character factory.
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            air_jump_available = reinterpret_cast<bool (*)(UObject*)>(base + air_jump_available_rva)(pawn);
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
            zoom = camera_distance(pawn);
            orbit = camera_orbit(pawn);
            Call frame_delta(L"/Script/Engine.Default__GameplayStatics", L"GetWorldDeltaSeconds");
            frame_delta.object(L"WorldContextObject", pawn);
            frame_delta.run();
            camera_frame_delta = std::clamp(
                *frame_delta.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(frame_delta.parameters.data()),
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
            const bool starting_motion = motion_name.starts_with(L"RunStart") || motion_name.starts_with(L"WalkStart");
            const bool looping_motion = motion_name.starts_with(L"RunLoop") || motion_name.starts_with(L"WalkLoop");
            const float pending_movement = movement_input[0] * movement_input[0] + movement_input[1] * movement_input[1];
            const float previous_movement = last_movement_input[0] * last_movement_input[0] +
                                            last_movement_input[1] * last_movement_input[1];
            const float stick_movement = stick_input.processed[0] * stick_input.processed[0] +
                                         stick_input.processed[1] * stick_input.processed[1];
            run_handoff = running && (starting_motion || looping_motion) &&
                          (run_step == run_loop_phase || run_step == run_entry_phase ||
                           (run_step == run_stance_blend_phase && looping_motion)) &&
                          (velocity[0] * velocity[0] + velocity[1] * velocity[1] > run_speed_epsilon_squared ||
                           pending_movement > movement_intent_epsilon_squared ||
                           previous_movement > movement_intent_epsilon_squared || stick_movement > movement_intent_epsilon_squared);
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
                Call loop(anim, L"SQEX_IsAnimLoop");
                loop.name(L"InSlotName", L"FullBody");
                loop.run();
                // A stance blend can report a non-looping slot while the
                // selected motion is already the next authored run loop.
                run_loop = looping_motion || loop.boolean_result();
                run_phase = length > 0.0f ? (run_loop ? std::fmod(time / length, 1.0f) : std::clamp(time / length, 0.0f, 1.0f)) : 0.0f;
                run_until = Clock::now() + switch_timeout;
            }
            auto* mode =
                is_uobject_valid_fast(movement) ? movement->GetPropertyByNameInChain(L"MovementMode") : nullptr;
            falling = mode && mode->GetElementSize() == 1 && *mode->ContainerPtrToValuePtr<uint8_t>(movement) == 3;
        }

        bool request(UObject* pawn)
        {
            if (queued || !available(pawn) || !install())
            {
                return false;
            }
            int captured_index = -1;
            bool captured_air = false;
            auto* current = state(pawn);
            if (attack_state(current, pawn) && !field<uint8_t>(current, combo_finisher_offset))
            {
                const int index = field<int>(current, combo_index_offset);
                if (index >= 0 && index < 64)
                {
                    captured_index = index + 1;
                    captured_air = field<uint8_t>(current, combo_air_offset) != 0;
                }
            }
            else if (incoming.Get() == pawn && Clock::now() <= combo_until)
            {
                captured_index = next_index;
                captured_air = air;
            }
            capture_frame(pawn);
            outgoing = make_uobject_weak(pawn);
            request_world = make_uobject_weak(kh02_actor_world(pawn));
            incoming = {};
            positioned = {};
            next_index = captured_index;
            air = captured_air;
            target = kh02_current_story_world()
                         ? (player_id(pawn) == player_id_value(PlayerId::Sora) ? player_id_value(PlayerId::Aqua)
                                                                               : player_id_value(PlayerId::Sora))
                         : character_swap::swap_target(pawn);
            requested_at = Clock::now();
            waiting_for_preload = false;
            attack_input_until = requested_at + switch_timeout + combo_grace;
            combo_until = {};
            if (!kh02_current_story_world() && !character_shotlock::begin_swap_input_check(pawn))
            {
                return false;
            }
            queued = true;
            dispatched = false;
            input_seeded = false;
            error.clear();
            return true;
        }

        void complete_first_movement(Clock::time_point now)
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
            if (stick_distance_squared(input.processed, stick_input.processed) > stick_continuity_epsilon_squared ||
                stick_distance_squared(input.raw, stick_input.raw) > stick_continuity_epsilon_squared)
            {
                return;
            }
            const auto current_velocity = vector(pawn, L"GetVelocity");
            if (current_velocity[0] * current_velocity[0] + current_velocity[1] * current_velocity[1] <= run_speed_epsilon_squared)
            {
                return;
            }
            Call frame(L"/Script/Engine.Default__GameplayStatics", L"GetWorldDeltaSeconds");
            frame.object(L"WorldContextObject", pawn);
            frame.run();
            const float delta = *frame.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(frame.parameters.data());
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

        auto tick(Clock::time_point now) -> std::optional<int>
        {
            complete_first_movement(now);
            if (attack_press_held && now >= attack_release_at)
            {
                release_attack_press();
            }
            attack_buffer.expire(now);
            if (!queued)
            {
                character_shotlock::finish_swap_input_check();
                tick_attack_buffer(now);
                if (next_index >= 0 && (now > combo_until || player() != incoming.Get() ||
                                        request_world.Get() != swap_context_world() || !available(incoming.Get())))
                {
                    next_index = -1;
                    incoming = {};
                }
                return std::nullopt;
            }
            // Keep the existing weak transaction and capture movement only at
            // dispatch. Waiting after the factory request would preserve an old
            // position/camera sample and consume the two-second handoff deadline.
            const bool preload_pending = !kh02_current_story_world() && character_swap::preparation_pending(target);
            if (!dispatched && request_world.Get() == swap_context_world() && player() == outgoing.Get() &&
                preload_pending && now - requested_at < std::chrono::seconds(30))
            {
                waiting_for_preload = true;
                return std::nullopt;
            }
            if (waiting_for_preload && !dispatched && !preload_pending)
            {
                if (!kh02_current_story_world() && !character_shotlock::begin_swap_input_check(player()))
                {
                    queued = false;
                    attack_buffer.clear();
                    error = "kh02_combat_swap_input_context";
                    return std::nullopt;
                }
                requested_at = now;
                attack_input_until = now + switch_timeout + combo_grace;
                next_index = -1;
                auto* current = state(player());
                if (attack_state(current, player()) && !field<uint8_t>(current, combo_finisher_offset))
                {
                    const int current_index = field<int>(current, combo_index_offset);
                    if (current_index >= 0 && current_index < 64)
                    {
                        next_index = current_index + 1;
                        air = field<uint8_t>(current, combo_air_offset) != 0;
                    }
                }
                waiting_for_preload = false;
            }
            if (request_world.Get() != swap_context_world() || now - requested_at > switch_timeout)
            {
                character_shotlock::finish_swap_input_check();
                queued = false;
                next_index = -1;
                attack_buffer.clear();
                error = "kh02_combat_swap_context_or_timeout";
                return std::nullopt;
            }
            auto* pawn = player();
            if (!dispatched)
            {
                if (pawn != outgoing.Get() || !available(pawn))
                {
                    character_shotlock::finish_swap_input_check();
                    queued = false;
                    next_index = -1;
                    attack_buffer.clear();
                    attack_input_until = {};
                    error = "kh02_combat_swap_dispatch_rejected";
                    return std::nullopt;
                }
                if (!kh02_current_story_world())
                {
                    const auto input_status = character_shotlock::swap_input_status(pawn);
                    if (input_status == character_shotlock::SwapInputStatus::Waiting)
                    {
                        return std::nullopt;
                    }
                    if (input_status == character_shotlock::SwapInputStatus::Blocked)
                    {
                        character_shotlock::finish_swap_input_check();
                        queued = false;
                        next_index = -1;
                        attack_buffer.clear();
                        attack_input_until = {};
                        error = "kh02_combat_swap_aim_input_rejected";
                        return std::nullopt;
                    }
                }
                capture_frame(pawn);
                if (target == static_cast<int>(PlayerId::KingMickey))
                {
                    swing_pending = false;
                    next_index = -1;
                    attack_buffer.clear();
                }
                release_attack_press();
                attack_buffer.handoff();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                if (test_movement)
                {
                    release_test_input();
                }
#endif
                if (!change(pawn, target))
                {
                    character_shotlock::finish_swap_input_check();
                    queued = false;
                    next_index = -1;
                    attack_buffer.clear();
                    attack_input_until = {};
                    error = "kh02_combat_swap_dispatch_rejected";
                    return std::nullopt;
                }
                character_shotlock::finish_swap_input_check();
                dispatched = true;
            }
            // The local player can render its newly possessed pawn before the
            // UI/save context and normal gameplay state have caught up. Apply
            // the visual handoff there immediately, before those later gates.
            pawn = local_player_pawn(request_world.Get());
            if (player_id(pawn) != target || pawn == outgoing.Get())
            {
                return std::nullopt;
            }
            auto* controller = object(pawn, L"Controller");
            if (!input_seeded && is_uobject_valid_fast(object(controller, L"PlayerInput")))
            {
                runtime_controller_restore_stick(controller, false, stick_input);
                runtime_controller_restore_stick(controller, true, look_input);
                input_seeded = true;
            }
            if (positioned.Get() != pawn)
            {
                if (floating_mode && character_swap::owns_swap_pawn(pawn) &&
                    (!character_exploration::prepare_pawn(pawn) || !character_traversal::prepare(pawn)))
                {
                    // A fresh alternate may omit Floating from its authored
                    // state set. Bind the incoming queue and motions before
                    // the same transaction requests that shared state.
                    return std::nullopt;
                }
                auto* camera = object(camera_manager(pawn), L"m_CameraNormal1st");
                if (!is_uobject_valid_fast(object(camera, L"RootComponent")))
                {
                    return std::nullopt;
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
                if (!kh02_current_story_world())
                {
                    // Only the incoming owned pawn is changed, during the existing
                    // possession transaction. Ordinary swaps also clear stale bits.
                    auto& floating_flags = *reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(pawn) + 0xd80);
                    floating_flags = (floating_flags & ~3u) | floating_mode;
                    if (floating_mode)
                    {
                        auto* component = object(pawn, L"MyStateComp");
                        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                        reinterpret_cast<void (*)(UObject*)>(base + queue_clear_rva)(component);
                        floating_flags = (floating_flags & ~3u) | floating_mode;
                        auto* floating = reinterpret_cast<UObject* (*)(UObject*, int)>(base + queue_push_rva)(
                            component, player_state_ids::floating);
                        if (!is_uobject_valid_fast(floating) || object(floating, L"MyCharPawn") != pawn)
                        {
                            throw std::runtime_error("character_floating_state_creation");
                        }
                        reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component, 0.0f);
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
                reinterpret_cast<void (*)(UObject*, bool)>(base + set_air_jump_available_rva)(pawn, air_jump_available);
                character_traversal::apply_movement(pawn);
                auto* speed =
                    is_uobject_valid_fast(movement) ? movement->GetPropertyByNameInChain(L"Velocity") : nullptr;
                if (speed && speed->GetElementSize() == sizeof(velocity))
                {
                    std::memcpy(speed->ContainerPtrToValuePtr<void>(movement), velocity.data(), sizeof(velocity));
                }
                character_camera::match_field_height(pawn);
                restore_camera(pawn, control_rotation, rendered_view, rendered_timestamp, zoom, orbit);
                transition_camera = make_uobject_weak(camera);
                camera_transition_frames = 2;
                restore_movement(pawn);
                resume_swing(pawn);
                first_movement_pending = run_handoff && run_loop && !falling &&
                                         stick_distance_squared(stick_input.processed, {}) > movement_intent_epsilon_squared &&
                                         stick_distance_squared(stick_input.raw, {}) > movement_intent_epsilon_squared &&
                                         stick_distance_squared(stick_input.raw, stick_input.committed_raw) <= stick_continuity_epsilon_squared &&
                                         velocity[0] * velocity[0] + velocity[1] * velocity[1] > run_speed_epsilon_squared;
                if (first_movement_pending)
                {
                    first_movement_pawn = make_uobject_weak(pawn);
                    first_movement_location = vector(pawn, L"K2_GetActorLocation");
                    first_movement_deadline = Clock::now() + first_movement_grace;
                }
                positioned = make_uobject_weak(pawn);
            }
            if (input_seeded)
            {
                resume_run(pawn);
            }
            if (!input_seeded || pawn != player() || !available(pawn))
            {
                return std::nullopt;
            }
            incoming = make_uobject_weak(pawn);
            combo_until = std::max(combo_until, now + combo_grace);
            attack_input_until = combo_until;
            queued = false;
            tick_attack_buffer(now);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++swaps;
#endif
            return target;
        }

        bool swing_damage(UObject* victim, UObject* instigator, UObject* causer, const void* event, bool record)
        {
            auto* owner = swing_owner.Get();
            if (GetCurrentThreadId() != game_thread || !is_uobject_valid_fast(victim) ||
                !is_uobject_valid_fast(owner) || kh02_actor_world(owner) != swap_context_world() ||
                state(owner) != swing_state.Get())
            {
                return false;
            }
            // Match native challenge attribution, including weapon-caused hits.
            auto* controller = object(owner, L"Controller");
            if (causer != owner && (!is_uobject_valid_fast(controller) || instigator != controller))
            {
                return false;
            }
            const auto metadata = kh02_decode_damage_event(event);
            // Native normal sword hits use command 27. This includes elemental
            // normal finishers, while excluding lingering spells and shotlocks.
            if (!metadata || metadata->command != 27)
            {
                return false;
            }
            const auto weak = make_uobject_weak(victim);
            uint64_t identity{};
            static_assert(sizeof(weak) == sizeof(identity));
            std::memcpy(&identity, &weak, sizeof(identity));
            if (record)
            {
                swing_hits.record(identity);
                return false;
            }
            const bool blocked = swing_hits.blocked(identity);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (blocked)
            {
                ++duplicate_hits_blocked;
            }
#endif
            return blocked;
        }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        void release_test_input()
        {
            if (test_movement)
            {
                auto* controller = test_locomotion ? object(local_player_pawn(test_world.Get()), L"Controller") : test_controller.Get();
                if (is_uobject_valid_fast(controller) &&
                    object(controller, L"Pawn") == local_player_pawn(test_world.Get()) &&
                    object(object(controller, L"Player"), L"PlayerController") == controller)
                {
                    runtime_controller_stick_input(controller, 0.0f, 0.0f);
                }
            }
            if (test_camera || test_rotate)
            {
                if (auto* controller = test_controller.Get(); is_uobject_valid_fast(controller))
                {
                    runtime_controller_look_input(controller, 0.0f, 0.0f);
                }
            }
            if (test_held)
            {
                const bool consumed = test_action == L"attack" && attack_key(0, L"Gamepad_FaceButton_Bottom", 1);
                if (auto* controller = test_controller.Get();
                    !consumed && is_uobject_valid_fast(controller) &&
                    object(object(controller, L"Player"), L"PlayerController") == controller)
                {
                    runtime_controller_action_input(controller, test_action, false);
                }
                test_held = false;
            }
        }

        void test_press(UObject* pawn, const wchar_t* action, Clock::time_point now)
        {
            auto* controller = object(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller) || object(controller, L"Pawn") != pawn)
            {
                throw std::runtime_error("kh02_combat_test_controller");
            }
            test_controller = make_uobject_weak(controller);
            test_action = action;
            const bool consumed = test_action == L"attack" && attack_key(0, L"Gamepad_FaceButton_Bottom", 0);
            if (!consumed)
            {
                runtime_controller_action_input(controller, action, true);
            }
            test_held = true;
            test_release = now + std::chrono::milliseconds(50);
            test_next = now + std::chrono::milliseconds(180);
        }

        bool start_test(bool airborne, bool expire, float phase = 0.35f)
        {
            auto* pawn = player();
            if (test_phase || queued || !available(pawn) || !install())
            {
                return false;
            }
            test_world = make_uobject_weak(kh02_actor_world(pawn));
            runtime_controller_test_stick_lease(nullptr, false);
            test_camera = false;
            test_movement = false;
            test_rotate = false;
            test_locomotion = false;
            test_menu = false;
            test_target = kh02_current_story_world()
                              ? (player_id(pawn) == player_id_value(PlayerId::Sora) ? player_id_value(PlayerId::Aqua)
                                                                                    : player_id_value(PlayerId::Sora))
                              : character_swap::swap_target(pawn);
            test_expected_index = expire ? 0 : airborne ? 1 : 2;
            test_air = airborne;
            test_input_mode = 0;
            test_origin = player_id(pawn);
            test_expire = expire;
            test_swing_phase = phase;
            test_jump = airborne;
            test_until = Clock::now() + std::chrono::seconds(6);
            test_next = Clock::now();
            test_status = "running";
            test_result = nullptr;
            test_phase = 1;
            return true;
        }

        bool start_menu_test()
        {
            if (!kh02_current_story_world() || player_id(player()) != player_id_value(PlayerId::Sora) ||
                !start_test(false, false))
            {
                return false;
            }
            test_menu = true;
            test_result = {{"spell_state_seen", false}, {"buffered_before", buffered_presses},
                           {"replayed_before", replayed_presses}};
            return true;
        }

        bool start_input_test(bool swap_back)
        {
            if (!start_test(false, false, 0.05f))
            {
                return false;
            }
            test_input_mode = swap_back ? 2 : 1;
            test_expected_index = swap_back ? 2 : 1;
            return true;
        }

        bool start_camera_test()
        {
            if (!start_test(false, false))
            {
                return false;
            }
            test_camera = true;
            test_controller = make_uobject_weak(object(player(), L"Controller"));
            runtime_controller_look_input(test_controller.Get(), 0.65f, 0.3f);
            test_next = Clock::now() + std::chrono::milliseconds(400);
            test_result = nlohmann::json::object();
            return true;
        }

        bool start_movement_test(bool rotate)
        {
            if (!start_test(false, false))
            {
                return false;
            }
            test_movement = true;
            test_rotate = rotate;
            test_controller = make_uobject_weak(object(player(), L"Controller"));
            runtime_controller_stick_input(test_controller.Get(), 0.6f, 0.8f);
            if (test_rotate)
            {
                runtime_controller_look_input(test_controller.Get(), 0.65f, 0.1f);
            }
            test_next = Clock::now() + std::chrono::milliseconds(1000);
            test_result = nlohmann::json::object();
            return true;
        }

        auto movement_snapshot(UObject* pawn) -> nlohmann::json
        {
            const auto distance = camera_distance(pawn);
            auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            auto* current = state(pawn);
            return {{"velocity", vector(pawn, L"GetVelocity")},
                    {"location", vector(pawn, L"K2_GetActorLocation")},
                    {"stick", movement_stick(pawn)},
                    {"look", runtime_controller_stick_state(object(pawn, L"Controller"), true)},
                    {"camera", vector(camera_manager(pawn), L"GetCameraRotation")},
                    {"state", state_id(current)},
                    {"step", property_value<int>(current, L"m_nSubStep")},
                    {"anim_time", animation_value(anim, L"SQEX_AnimGetCurrentTime")},
                    {"anim_length", animation_value(anim, L"SQEX_AnimGetCurrentLength")},
                    {"distance", {distance.current, distance.battle, distance.normal}}};
        }

        bool start_locomotion_test(int count, int warmup_ms, int strength_percent, bool release_at_swap,
                                   bool camera_relative, bool capture_gait)
        {
            if (count < 0 || count > 8 || warmup_ms < 0 || warmup_ms > 2500 ||
                strength_percent < 1 || strength_percent > 100 || (release_at_swap && count != 1) || !start_test(false, false))
            {
                return false;
            }
            test_movement = true;
            test_locomotion = true;
            test_camera_relative = camera_relative;
            runtime_controller_test_stick_lease(object(player(), L"Controller"), true);
            test_release_at_swap = release_at_swap;
            test_capture_gait = capture_gait;
            test_gait_bones.clear();
            test_swaps_remaining = count;
            test_strength = strength_percent / 100.0f;
            test_started = Clock::now();
            test_next = test_started + std::chrono::milliseconds(warmup_ms);
            test_result = {{"samples", nlohmann::json::array()}, {"requests", nlohmann::json::array()}};
            return true;
        }

        void sample_gait(UObject* pawn, UObject* anim, nlohmann::json& sample)
        {
            auto* mesh = object(pawn, L"MyMesh");
            const int role = player_id(pawn);
            auto found = test_gait_bones.find(role);
            if (found == test_gait_bones.end())
            {
                Call sockets(mesh, L"GetAllSocketNames");
                sockets.run();
                auto* returned = sockets.param(L"ReturnValue");
                if (!returned || !returned->IsA<FArrayProperty>() ||
                    static_cast<FArrayProperty*>(returned)->GetInner()->GetElementSize() != sizeof(FName))
                {
                    throw std::runtime_error("locomotion_gait_socket_layout");
                }
                const auto& names = *returned->ContainerPtrToValuePtr<TArray<FName>>(sockets.parameters.data());
                if (names.Num() < 0 || names.Num() > 4096)
                {
                    throw std::runtime_error("locomotion_gait_socket_count");
                }
                std::vector<std::wstring> bones;
                auto all_names = nlohmann::json::array();
                for (const auto& name : names)
                {
                    const auto text = name.ToString();
                    all_names.push_back(to_utf8(text));
                    // The playable KH3 skeletons name the ankle/toe pairs
                    // ashi1/ashi2. Resolve only names actually on this mesh.
                    if (text == L"L_ashi1" || text == L"R_ashi1" ||
                        text == L"L_ashi2" || text == L"R_ashi2")
                    {
                        bones.push_back(text);
                    }
                }
                test_result["gait_bone_names"][std::to_string(role)] = std::move(all_names);
                found = test_gait_bones.emplace(role, std::move(bones)).first;
            }
            sample["anim_play_rate"] = animation_value(anim, L"SQEX_AnimGetCurrentPlayRate");
            sample["mesh_anim_rate_scale"] = property_value<float>(mesh, L"GlobalAnimRateScale");
            sample["feet"] = nlohmann::json::object();
            for (const auto& bone : found->second)
            {
                Call position(mesh, L"GetSocketLocation");
                position.name(L"InSocketName", bone.c_str());
                position.run();
                sample["feet"][to_utf8(bone)] = position.result_vector();
            }
        }

        void tick_locomotion_test(Clock::time_point now)
        {
            if (!queued && test_swaps_remaining == 0 && now >= test_next)
            {
                runtime_controller_test_stick_lease(nullptr, false);
                // InputAxis accumulates samples within a frame. Release on a
                // frame that has not already received the held test sample.
                release_test_input();
                test_phase = 0;
                test_status = "completed";
                return;
            }
            auto* pawn = local_player_pawn(test_world.Get());
            auto* controller = object(pawn, L"Controller");
            if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(object(controller, L"PlayerInput")))
            {
                return;
            }
            // Hold either a world direction or the same physical stick axes.
            // The latter exposes camera-basis changes during possession.
            const auto pending_input = runtime_controller_capture_stick(controller, false);
            if (test_release_at_swap && test_swaps_remaining == 0 && player_id(pawn) != test_origin)
            {
                // A device zero arrives after the old held state was restored,
                // before the new controller's first completed input pass.
                runtime_controller_stick_input(controller, 0.0f, 0.0f);
                test_strength = 0.0f;
                test_release_at_swap = false;
                test_result["released_ms"] = std::chrono::duration<double, std::milli>(now - test_started).count();
            }
            else if (!pending_input.pending_samples[0] && !pending_input.pending_samples[1] &&
                     (test_controller.Get() == controller || !input_seeded || positioned.Get() != pawn))
            {
                if (test_camera_relative)
                {
                    runtime_controller_stick_input(controller, 0.0f, test_strength);
                }
                else
                {
                    runtime_controller_move_input(controller, test_strength, 0.0f);
                }
            }
            test_controller = make_uobject_weak(controller);
            auto* movement = object(pawn, L"MyMovement");
            auto* current = state(pawn);
            if (!is_uobject_valid_fast(movement))
            {
                return;
            }
            const bool running = is_uobject_valid_fast(current) &&
                                 current->GetClassPrivate()->GetName() == L"TresPlayerState_Run";
            nlohmann::json sample = {{"velocity", vector(pawn, L"GetVelocity")},
                                     {"location", vector(pawn, L"K2_GetActorLocation")},
                                     {"stick", movement_stick(pawn)},
                                     {"state", state_id(current)},
                                     {"step", is_uobject_valid_fast(current) ? property_value<int>(current, L"m_nSubStep") : -1}};
            sample["ms"] = std::chrono::duration<double, std::milli>(now - test_started).count();
            sample["player"] = player_id(pawn);
            sample["camera"] = vector(camera_manager(pawn), L"GetCameraRotation");
            const auto view = field<CameraView>(camera_pov(camera_manager(pawn), L"CameraCache"), 0);
            sample["camera_location"] = view.location;
            sample["camera_fov"] = view.fov;
            const auto distance = camera_distance(pawn);
            sample["camera_distance"] = {distance.current, distance.battle, distance.normal};
            sample["camera_pivot"] = camera_orbit(pawn).pivot;
            sample["camera_timestamp"] = *camera_timestamp(camera_manager(pawn));
            sample["control_rotation"] = vector(controller, L"GetControlRotation");
            sample["actor_rotation"] = vector(pawn, L"K2_GetActorRotation");
            sample["run_heading"] = running ? field<float>(current, 0x8c) : 0.0f;
            sample["pending"] = queued;
            sample["first_movement_updates"] = first_movement_updates;
            const auto stick = runtime_controller_capture_stick(controller, false);
            sample["pending_stick"] = stick.raw;
            sample["processed_stick"] = stick.processed;
            sample["acceleration"] = property_value<std::array<float, 3>>(movement, L"Acceleration");
            sample["max_speed"] = property_value<float>(movement, L"MaxWalkSpeed");
            sample["analog"] = property_value<float>(movement, L"AnalogInputModifier");
            sample["run_elapsed"] = running ? field<float>(current, 0xb8) : 0.0f;
            sample["sprinting"] = running && field<uint8_t>(current, 0xbc) != 0;
            if (running)
            {
                const auto animation_name = field<FName>(current, 0xa0).ToString();
                sample["run_animation"] = std::string(animation_name.begin(), animation_name.end());
                auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
                Call section(anim, L"SQEX_AnimGetCurrentSectionName");
                section.name(L"InSlotName", L"FullBody");
                section.run();
                auto* section_result = section.param(L"ReturnValue");
                if (section_result->GetElementSize() != sizeof(FName))
                {
                    throw std::runtime_error("locomotion_section_name_layout");
                }
                const auto section_name = section_result->ContainerPtrToValuePtr<FName>(section.parameters.data())->ToString();
                sample["section"] = std::string(section_name.begin(), section_name.end());
                sample["anim_length"] = animation_value(anim, L"SQEX_AnimGetCurrentLength");
                sample["anim_time"] = animation_value(anim, L"SQEX_AnimGetCurrentTime");
                if (test_capture_gait)
                {
                    sample_gait(pawn, anim, sample);
                }
            }
            Call delta(L"/Script/Engine.Default__GameplayStatics", L"GetWorldDeltaSeconds");
            delta.object(L"WorldContextObject", pawn);
            delta.run();
            sample["world_delta"] = *delta.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(delta.parameters.data());
            if (const auto frame = runtime_engine_last_frame_sample())
            {
                sample["previous_engine_frame"] = {
                    {"begin_us", frame->begin_us},
                    {"engine_end_us", frame->engine_end_us},
                    {"callback_end_us", frame->callback_end_us},
                    {"foreground", frame->foreground},
                    {"idle", frame->idle}};
            }
            if (auto* last_update = movement->GetPropertyByNameInChain(L"LastUpdateLocation");
                last_update && last_update->GetElementSize() == sizeof(std::array<float, 3>))
            {
                sample["movement_tick"] = property_value<std::array<float, 3>>(movement, L"LastUpdateLocation");
            }
            test_result["samples"].push_back(std::move(sample));
            if (now < test_next || queued)
            {
                return;
            }
            if (pawn == player() && available(pawn) && request(pawn))
            {
                --test_swaps_remaining;
                test_result["requests"].push_back({{"ms", std::chrono::duration<double, std::milli>(now - test_started).count()},
                                                    {"player", player_id(pawn)}});
                test_next = now + std::chrono::milliseconds(test_swaps_remaining ? 150 : 500);
            }
        }

        void tick_movement_test(UObject* pawn, Clock::time_point now)
        {
            if (now < test_next)
            {
                return;
            }
            if (test_phase == 1)
            {
                test_result["before"] = movement_snapshot(pawn);
                if (!request(pawn))
                {
                    throw std::runtime_error("kh02_movement_test_swap");
                }
                test_phase = 2;
            }
            else if (test_phase == 2 && player_id(pawn) == test_target)
            {
                test_controller = make_uobject_weak(object(pawn, L"Controller"));
                test_result["at_swap"] = movement_snapshot(pawn);
                test_next = now + std::chrono::milliseconds(400);
                test_phase = 3;
            }
            else if (test_phase == 3)
            {
                test_result["after"] = movement_snapshot(pawn);
                release_test_input();
                test_status = "completed";
                test_phase = 0;
            }
        }

        void tick_camera_test(UObject* pawn, Clock::time_point now)
        {
            if (test_phase == 4 && !test_result.contains("first_frame"))
            {
                test_result["first_frame"] = vector(camera_manager(pawn), L"GetCameraRotation");
            }
            if (now < test_next)
            {
                return;
            }
            if (test_phase == 1)
            {
                release_test_input();
                test_next = now + std::chrono::milliseconds(100);
                test_phase = 2;
            }
            else if (test_phase == 2)
            {
                test_result["before"] = vector(camera_manager(pawn), L"GetCameraRotation");
                test_result["control_before"] = vector(object(pawn, L"Controller"), L"GetControlRotation");
                if (!request(pawn))
                {
                    throw std::runtime_error("kh02_camera_test_swap");
                }
                test_phase = 3;
            }
            else if (test_phase == 3 && player_id(pawn) == test_target)
            {
                test_result["created_cache"] = vector(camera_manager(pawn), L"GetCameraRotation");
                for (const auto* name : {L"CameraCache", L"LastFrameCameraCache", L"ViewTarget", L"PendingViewTarget"})
                {
                    const auto view = field<CameraView>(camera_pov(camera_manager(pawn), name), 0);
                    test_result["creation_views"].push_back(view.rotation);
                }
                test_next = now + std::chrono::milliseconds(800);
                test_phase = 4;
            }
            else if (test_phase == 4)
            {
                test_result["after"] = vector(camera_manager(pawn), L"GetCameraRotation");
                test_result["control_after"] = vector(object(pawn, L"Controller"), L"GetControlRotation");
                test_controller = make_uobject_weak(object(pawn, L"Controller"));
                runtime_controller_look_input(test_controller.Get(), -0.5f, -0.3f);
                test_next = now + std::chrono::milliseconds(300);
                test_phase = 5;
            }
            else if (test_phase == 5)
            {
                release_test_input();
                test_result["after_look"] = vector(camera_manager(pawn), L"GetCameraRotation");
                test_status = "completed";
                test_phase = 0;
            }
        }

        void tick_menu_test(UObject* pawn, Clock::time_point now)
        {
            if (queued || now < test_next)
            {
                return;
            }
            if (test_phase == 1)
            {
                if (!available(pawn) || !request(pawn))
                {
                    throw std::runtime_error("kh02_menu_test_swap");
                }
                test_phase = 2;
            }
            else if (test_phase == 2 && player_id(pawn) == test_target && available(pawn))
            {
                test_result["mp_before"] = property_value<int>(pawn, L"m_MagicPoint");
                test_press(pawn, L"command_down", now);
                test_phase = 3;
            }
            else if (test_phase == 3 && !test_held)
            {
                // One physical confirm, routed through the real swap buffer.
                // It must open Magic without confirming a spell on a retry.
                test_press(pawn, L"attack", now);
                test_next = now + std::chrono::milliseconds(100);
                test_phase = 4;
            }
            else if (test_phase == 4)
            {
                auto* current = state(pawn);
                if (is_uobject_valid_fast(current) &&
                    current->GetClassPrivate()->GetName() == L"TresPlayerState_FireMagic")
                {
                    test_result["spell_state_seen"] = true;
                }
                if (now < test_release + std::chrono::milliseconds(1000))
                {
                    return;
                }
                const int mp = property_value<int>(pawn, L"m_MagicPoint");
                test_result["mp_after"] = mp;
                test_result["buffered_after"] = buffered_presses;
                test_result["replayed_after"] = replayed_presses;
                test_status = test_result["spell_state_seen"].get<bool>() || mp < test_result["mp_before"].get<int>()
                                  ? "failed_unrequested_cast"
                                  : "completed";
                test_phase = 0;
            }
        }

        void tick_test(Clock::time_point now)
        {
            if (!test_phase)
            {
                return;
            }
            if (test_held && now >= test_release)
            {
                release_test_input();
            }
            if (now > test_until || test_world.Get() != swap_context_world())
            {
                runtime_controller_test_stick_lease(nullptr, false);
                release_test_input();
                test_status = "failed_context_or_timeout";
                test_phase = 0;
                return;
            }
            auto* pawn = player();
            if (test_menu)
            {
                tick_menu_test(pawn, now);
                return;
            }
            if (!test_camera && test_phase == 4 && !test_held)
            {
                test_status = "completed";
                test_phase = 0;
                return;
            }
            if (test_locomotion)
            {
                tick_locomotion_test(now);
                return;
            }
            if (queued || !available(pawn))
            {
                return;
            }
            if (test_camera)
            {
                tick_camera_test(pawn, now);
                return;
            }
            if (test_movement)
            {
                tick_movement_test(pawn, now);
                return;
            }
            if (test_phase == 1)
            {
                auto* current = state(pawn);
                const int wanted = test_air || test_input_mode ? 0 : 1;
                if (attack_state(current, pawn) && field<int>(current, combo_index_offset) == wanted &&
                    !field<uint8_t>(current, combo_finisher_offset) &&
                    (field<uint8_t>(current, combo_air_offset) != 0) == test_air)
                {
                    auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
                    const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
                    if (length > 0.0f && animation_value(anim, L"SQEX_AnimGetCurrentTime") / length < test_swing_phase)
                    {
                        return;
                    }
                    release_test_input();
                    if (!request(pawn))
                    {
                        test_status = "failed_swap_request";
                        test_phase = 0;
                        return;
                    }
                    test_phase = 2;
                    test_next = now + (test_expire ? std::chrono::milliseconds(1200) : std::chrono::milliseconds(100));
                    if (test_input_mode)
                    {
                        // One press immediately after requesting the swap,
                        // before the new controller is ready. No retries.
                        test_press(pawn, L"attack", now);
                        test_phase = test_input_mode == 2 ? 5 : 3;
                    }
                    return;
                }
                if (!test_held && now >= test_next)
                {
                    test_press(pawn, test_jump ? L"jump" : L"attack", now);
                    if (test_jump)
                    {
                        test_jump = false;
                        test_next = now + std::chrono::milliseconds(100);
                    }
                }
            }
            else if (test_phase == 2 && player_id(pawn) == test_target && now >= test_next)
            {
                if (test_expire && now <= combo_until + std::chrono::milliseconds(100))
                {
                    return;
                }
                if (!test_expire && !test_input_mode && attack_state(state(pawn), pawn))
                {
                    auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
                    const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
                    if (length > 0.0f && animation_value(anim, L"SQEX_AnimGetCurrentTime") / length < 0.55f)
                    {
                        return;
                    }
                }
                test_press(pawn, L"attack", now);
                test_phase = 3;
            }
            else if (test_phase == 3 && !selections.empty())
            {
                const auto& selected = selections.back();
                if (selected["player"] == test_target && !selected.value("resumed_swing", false))
                {
                    if (test_input_mode == 2 && selected["index"].get<int>() < test_expected_index &&
                        attack_buffer.front())
                    {
                        return;
                    }
                    test_result = selected;
                    if (selected["index"] != test_expected_index || (test_expire && selected["handoff"] != false) ||
                        selected["air"] != test_air)
                    {
                        release_test_input();
                        test_status = "failed_selection";
                        test_phase = 0;
                        return;
                    }
                    test_phase = 4;
                }
            }
            else if (test_phase == 5 && player_id(pawn) == test_target)
            {
                release_test_input();
                if (!request(pawn))
                {
                    throw std::runtime_error("kh02_input_test_swap_back");
                }
                test_target = test_origin;
                test_press(pawn, L"attack", now);
                test_phase = 3;
            }
        }
#endif
    };

    Kh02CombatSwap::Kh02CombatSwap(Player player, Change change) : player(std::move(player)), change(std::move(change))
    {
    }

    Kh02CombatSwap::~Kh02CombatSwap() = default;

    bool Kh02CombatSwap::initialize()
    {
        if (!impl && swap_context_world())
        {
            impl = std::make_unique<Impl>(player, change);
        }
        return impl != nullptr;
    }

    bool Kh02CombatSwap::request(UObject* pawn, bool trap_owned)
    {
        try
        {
            if (character_swap::manual_swaps_blocked() && !trap_owned)
            {
                return false;
            }
            return initialize() && impl->request(pawn);
        }
        catch (const std::exception& failure)
        {
            if (impl)
            {
                impl->error = failure.what();
            }
            return false;
        }
    }

    bool Kh02CombatSwap::available(UObject* pawn) const
    {
        try
        {
            return Impl::available(pawn);
        }
        catch (...)
        {
            return false;
        }
    }

    bool Kh02CombatSwap::busy() const
    {
        return impl && impl->queued;
    }

    bool Kh02CombatSwap::attack_key(int controller, std::wstring_view key, int event)
    {
        try
        {
            return impl && impl->attack_key(controller, key, event);
        }
        catch (const std::exception& failure)
        {
            impl->error = failure.what();
            return false;
        }
    }

    auto Kh02CombatSwap::tick(Clock::time_point now) -> std::optional<int>
    {
        if (!impl)
        {
            initialization.observe(world_context_revision.load(std::memory_order_acquire));
            if (!initialization.due(now))
            {
                return std::nullopt;
            }
            const bool settled = !swap_context_world() || initialize();
            initialization.complete(settled, now);
        }
        if (!impl)
        {
            return std::nullopt;
        }
        try
        {
            const auto result = impl->tick(now);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            impl->tick_test(now);
#endif
            return result;
        }
        catch (const std::exception& failure)
        {
            impl->error = failure.what();
            impl->queued = false;
            impl->next_index = -1;
            impl->attack_buffer.clear();
            impl->release_attack_press();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            impl->release_test_input();
            runtime_controller_test_stick_lease(nullptr, false);
            impl->test_phase = 0;
            impl->test_status = "failed_exception";
#endif
            return std::nullopt;
        }
    }

    bool Kh02CombatSwap::blocks_swing_damage(UObject* victim, UObject* instigator, UObject* causer, const void* event)
    {
        try
        {
            return Impl::active && Impl::active->swing_damage(victim, instigator, causer, event, false);
        }
        catch (...)
        {
            return false;
        }
    }

    void Kh02CombatSwap::record_swing_damage(UObject* victim, UObject* instigator, UObject* causer, const void* event)
    {
        try
        {
            if (Impl::active)
            {
                Impl::active->swing_damage(victim, instigator, causer, event, true);
            }
        }
        catch (...)
        {
        }
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool Kh02CombatSwap::give_combo_master()
    {
        if (!initialize())
        {
            return false;
        }
        auto* pawn = impl->player();
        const auto ability = try_resolve_runtime_enum_value(L"ETresAbilityKind::COMBO_MASTER");
        if (!impl->available(pawn) || player_id(pawn) != player_id_value(PlayerId::Sora) || !ability || *ability > 255)
        {
            return false;
        }
        RuntimeSoraAbilityEntryState state;
        bool granted{};
        std::wstring reason;
        return try_grant_unique_equipped_runtime_sora_ability(static_cast<uint8_t>(*ability), pawn, &granted, &state,
                                                              &reason) &&
               state.owned && state.equipped;
    }

    bool Kh02CombatSwap::start_test(bool air, bool expire, float phase)
    {
        return initialize() && impl->start_test(air, expire, phase);
    }

    bool Kh02CombatSwap::start_camera_test()
    {
        return initialize() && impl->start_camera_test();
    }

    bool Kh02CombatSwap::start_input_test(bool swap_back)
    {
        return initialize() && impl->start_input_test(swap_back);
    }

    bool Kh02CombatSwap::start_menu_test()
    {
        return initialize() && impl->start_menu_test();
    }

    bool Kh02CombatSwap::start_movement_test(bool rotate)
    {
        return initialize() && impl->start_movement_test(rotate);
    }

    bool Kh02CombatSwap::start_locomotion_test(int swaps, int warmup_ms, int strength_percent, bool release_at_swap,
                                              bool camera_relative, bool capture_gait)
    {
        return initialize() && impl->start_locomotion_test(swaps, warmup_ms, strength_percent, release_at_swap,
                                                         camera_relative, capture_gait);
    }

    auto Kh02CombatSwap::snapshot() const -> std::string
    {
        if (!impl)
        {
            return R"({"installed":false,"pending":false,"next_index":-1,"swaps":0,"error":"","selections":[],"test_status":"idle","test_result":null,"test_input_held":false})";
        }
        return nlohmann::json{{"installed", impl->installed},
                              {"pending", impl->queued},
                              {"next_index", impl->next_index},
                              {"swaps", impl->swaps},
                              {"camera_control_ready", impl->camera_control_ready},
                              {"camera_ui_current", impl->camera_ui_current},
                              {"camera_deltas", impl->camera_deltas},
                              {"swing_resume", impl->swing_resume},
                              {"duplicate_hits_blocked", impl->duplicate_hits_blocked},
                              {"buffered_presses", impl->buffered_presses},
                              {"replayed_presses", impl->replayed_presses},
                              {"accepted_presses", impl->accepted_presses},
                              {"pending_attack_inputs", impl->attack_buffer.size()},
                              {"injected_attack_held", impl->attack_press_held},
                              {"error", impl->error},
                              {"selections", impl->selections},
                              {"test_status", impl->test_status},
                              {"test_result", impl->test_result},
                              {"test_input_held", impl->test_held}}
            .dump();
    }
#endif
} // namespace kh3ap
