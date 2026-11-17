#include "kh3ap/core/objects/actor_owning_world.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/movement/character_floating.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/kairi_ground_slam.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_floating
{
    using namespace RC::Unreal;
    namespace
    {
        using Notify = void (*)(UObject*, bool, bool);
        using TornadoBegin = void (*)(UObject*);
        uint64_t original_notify{};
        uint64_t original_tornado{};
        uint64_t original_vacuum{}, original_vacuum_end{};
        std::unique_ptr<PLH::x64Detour> notify_hook;
        std::unique_ptr<PLH::x64Detour> tornado_hook;
        std::unique_ptr<PLH::x64Detour> vacuum_hook, vacuum_end_hook;
        FWeakObjectPtr freefall_world;
        FWeakObjectPtr vacuum_action;
        ReflectedPropertyCache<16> properties;

        bool tornado_world(UObject* world)
        {
            return is_uobject_valid_fast(world) &&
                world->GetPathName().starts_with(L"/Game/Levels/he/he_06/he_06.");
        }

        UObject* local_pawn()
        {
            auto* instance = properties.read_object(kh02_current_viewport(), L"GameInstance");
            auto* manager = properties.read_object(instance, L"m_UIManager");
            auto* controller = properties.read_object(manager, L"m_pPlayerController");
            auto* pawn = properties.read_object(controller, L"Pawn");
            auto* local = properties.read_object(controller, L"Player");
            return is_uobject_valid_fast(pawn) && properties.read_object(pawn, L"Controller") == controller &&
                properties.read_object(local, L"PlayerController") == controller &&
                actor_owning_world(pawn) == kh02_current_world() ? pawn : nullptr;
        }

        int state_id(UObject* pawn)
        {
            auto* current = properties.read_object(properties.read_object(pawn, L"MyStateComp"), L"m_pCurrentState");
            auto* id = properties.resolve(current, L"MyStateID");
            return id && id->GetElementSize() == 1 ? *id->ContainerPtrToValuePtr<uint8_t>(current) : 0;
        }

        void notify_floating(UObject* source_controller, bool enabled, bool special)
        {
            const auto original = reinterpret_cast<Notify>(original_notify);
            original(source_controller, enabled, special);
            auto* pawn = local_pawn();
            if (!pawn || !tornado_world(actor_owning_world(pawn)) ||
                actor_owning_world(source_controller) != actor_owning_world(pawn))
            {
                return;
            }
            auto* controller = properties.read_object(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller))
            {
                return;
            }
            // Preserve the actual event when its original controller was
            // replaced. Never clear queues or force Floating here: the native
            // freefall and ground slam must complete before the glide phase.
            if (controller != source_controller &&
                (freefall_world.Get() == kh02_current_world() || character_swap::owns_swap_pawn(pawn)))
            {
                original(controller, enabled, special);
            }
            // The authored mode event completes the dive, or resets the mode
            // on encounter entry/Retry. Retire the old lease at that event;
            // waiting for a swap attempt in Floating leaves it stuck on Retry.
            // The Jump/substep check still rejects an active dive independently.
            freefall_world = {};
            vacuum_action = {};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Tornado Floating event pawn={} enabled={} special={} state={} redirected={}\n"),
                pawn->GetName(), enabled, special, state_id(pawn), controller != source_controller);
#endif
        }

        void begin_tornado_phase(UObject* action, TornadoBegin original, bool suction)
        {
            auto* boss = properties.read_object(action, L"MyCharPawn");
            auto* world = actor_owning_world(boss);
            if (!tornado_world(world) || world != kh02_current_world())
            {
                original(action);
                return;
            }
            auto* pawn = local_pawn();
            try
            {
                if (character_swap::owns_swap_pawn(pawn) && !kairi_ground_slam::ready(pawn))
                {
                    pawn = character_swap::return_to_sora_for_encounter(pawn);
                }
            }
            catch (const std::exception& failure)
            {
                const std::string message = failure.what();
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Tornado Sora return failed: {}\n"),
                    std::wstring(message.begin(), message.end()));
                pawn = local_pawn();
            }
            // ChangePlayer clears the Titan's cached player. Its native action
            // consumes that reference but does not reacquire it. The replacement
            // must be locally possessed before the native sequence starts.
            auto* controller = properties.read_object(pawn, L"Controller");
            auto* local = properties.read_object(controller, L"Player");
            if (is_uobject_valid_fast(pawn) && actor_owning_world(pawn) == world &&
                properties.read_object(controller, L"Pawn") == pawn &&
                properties.read_object(local, L"PlayerController") == controller)
            {
                if (auto* cached = properties.resolve_object(boss, L"m_Player"))
                {
                    *cached->ContainerPtrToValuePtr<UObject*>(boss) = pawn;
                }
            }
            if (suction)
            {
                vacuum_action = make_uobject_weak(action);
            }
            else
            {
                freefall_world = make_uobject_weak(world);
            }
            original(action);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            auto* cached_player = properties.read_object(boss, L"m_Player");
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Tornado {} begin pawn={} cached_player={}\n"),
                suction ? L"suction" : L"freefall",
                is_uobject_valid_fast(pawn) ? pawn->GetName() : L"null",
                is_uobject_valid_fast(cached_player) ? cached_player->GetName() : L"null");
#endif
        }

        void begin_tornado(UObject* action)
        {
            begin_tornado_phase(action, reinterpret_cast<TornadoBegin>(original_tornado), false);
        }

        void begin_vacuum(UObject* action)
        {
            begin_tornado_phase(action, reinterpret_cast<TornadoBegin>(original_vacuum), true);
        }

        void end_vacuum(UObject* action)
        {
            reinterpret_cast<TornadoBegin>(original_vacuum_end)(action);
            if (vacuum_action.Get() == action)
            {
                vacuum_action = {};
            }
        }
    }

    void prepare_freefall(UObject* world)
    {
        if (tornado_world(world) && world == kh02_current_world())
        {
            freefall_world = make_uobject_weak(world);
        }
    }

    bool initialize()
    {
        if (notify_hook && tornado_hook && vacuum_hook && vacuum_end_hook)
        {
            return true;
        }
        using character_native::Site;
        if (!character_native::verify_sites({Site::floating_notify, Site::tornado_freefall_begin,
                                            Site::tornado_vacuum_begin, Site::tornado_vacuum_end}))
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        notify_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::floating_notify),
            reinterpret_cast<uintptr_t>(&notify_floating), &original_notify);
        tornado_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::tornado_freefall_begin),
            reinterpret_cast<uintptr_t>(&begin_tornado), &original_tornado);
        vacuum_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::tornado_vacuum_begin),
            reinterpret_cast<uintptr_t>(&begin_vacuum), &original_vacuum);
        vacuum_end_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::tornado_vacuum_end),
            reinterpret_cast<uintptr_t>(&end_vacuum), &original_vacuum_end);
        if (!notify_hook->hook() || !original_notify || !tornado_hook->hook() || !original_tornado ||
            !vacuum_hook->hook() || !original_vacuum || !vacuum_end_hook->hook() || !original_vacuum_end)
        {
            vacuum_end_hook.reset();
            vacuum_hook.reset();
            tornado_hook.reset();
            notify_hook.reset();
            return false;
        }
        return true;
    }

    bool restore_camera_view(UObject* pawn)
    {
        auto* world = actor_owning_world(pawn);
        if (!is_uobject_valid_fast(pawn) || !tornado_world(world) || world != kh02_current_world())
        {
            return false;
        }
        auto* controller = properties.read_object(pawn, L"Controller");
        auto* local = properties.read_object(controller, L"Player");
        auto* manager = properties.read_object(controller, L"PlayerCameraManager");
        if (properties.read_object(controller, L"Pawn") != pawn ||
            properties.read_object(local, L"PlayerController") != controller ||
            !is_uobject_valid_fast(manager) || actor_owning_world(manager) != world)
        {
            return false;
        }
        Kh02ReflectedCall active(manager, L"GetActiveCamera", "Tornado camera handoff", 256);
        active.run();
        auto* camera = active.result();
        if (!is_uobject_valid_fast(camera) || actor_owning_world(camera) != world ||
            camera->GetClassPrivate()->GetName() != L"BP_TresCameraHe903_C" ||
            properties.read_object(camera, L"m_CameraManager") != manager ||
            properties.read_object(camera, L"m_PlayerController") != controller)
        {
            return false;
        }
        Kh02ReflectedCall view(manager, L"GetActiveViewCamera", "Tornado camera handoff", 256);
        view.run();
        if (view.result() != controller)
        {
            return false;
        }
        // Reusing Sora's controller preserves the active arena-camera stack,
        // but possession resets its rendered view to the controller. That
        // unchanged stack does not select the camera again on the next tick.
        // Reconcile only this mismatch; preserve native movies and other views.
        Kh02ReflectedCall restore(controller, L"SetViewTargetWithBlend", "Tornado camera handoff", 256);
        restore.object(L"NewViewTarget", camera);
        restore.number(L"BlendTime", 0.0f);
        restore.run();
        view.run();
        const bool restored = view.result() == camera;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Tornado camera view restored={} pawn={} camera={}\n"),
            restored, pawn->GetName(), camera->GetName());
#endif
        return restored;
    }

    bool allows_swap(UObject* pawn)
    {
        if (character_native::build() == character_native::Build::Unsupported || !tornado_world(kh02_current_world()))
        {
            return true;
        }
        const int state = state_id(pawn);
        if (state == player_state_ids::floating && freefall_world.Get() == kh02_current_world())
        {
            freefall_world = {};
        }
        auto* current = properties.read_object(properties.read_object(pawn, L"MyStateComp"), L"m_pCurrentState");
        auto* step = properties.resolve(current, L"m_nSubStep");
        const bool diving = state == player_state_ids::jump && step &&
                            step->GetElementSize() == sizeof(int32_t) &&
                            *step->ContainerPtrToValuePtr<int32_t>(current) == 8;
        auto* vacuum_boss = properties.read_object(vacuum_action.Get(), L"MyCharPawn");
        const bool sucking = is_uobject_valid_fast(vacuum_boss) &&
                             actor_owning_world(vacuum_boss) == kh02_current_world();
        return !sucking && !diving && freefall_world.Get() != kh02_current_world();
    }
}
