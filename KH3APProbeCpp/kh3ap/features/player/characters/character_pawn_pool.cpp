#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_pawn_pool.h"
#include "kh3ap/features/player/characters/character_swap_stats.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <intrin.h>
#define NOMINMAX
#include <Windows.h>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include <nlohmann/json.hpp>
#endif

namespace kh3ap::character_pawn_pool
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr size_t maximum_pooled_actors = 32;
        constexpr int maximum_attached_children = 32;

        const uintptr_t spawn_rva = character_native::rva(character_native::Site::pool_spawn);
        const uintptr_t destroy_rva = character_native::rva(character_native::Site::pool_destroy);
        const uintptr_t seamless_from_rva = character_native::rva(character_native::Site::pool_seamless_from);
        const uintptr_t add_controller_rva = character_native::rva(character_native::Site::pool_add_controller);
        const uintptr_t remove_controller_rva = character_native::rva(character_native::Site::pool_remove_controller);
        const uintptr_t add_pawn_rva = character_native::rva(character_native::Site::pool_add_pawn);
        const uintptr_t remove_pawn_rva = character_native::rva(character_native::Site::pool_remove_pawn);
        const uintptr_t controller_spawn_return = character_native::rva(character_native::Site::pool_controller_spawn_call) + 5;
        const uintptr_t pawn_spawn_return = character_native::rva(character_native::Site::pool_pawn_spawn_call) + 5;
        constexpr size_t player_state_offset = 0x400;
        using Spawn = UObject* (*)(UObject*, UObject*, const void*, const void*);
        using Destroy = bool (*)(UObject*, UObject*, bool, bool);
        using SeamlessFrom = void (*)(UObject*, UObject*);
        using WorldActorRegistration = void (*)(UObject*, UObject*);
        uintptr_t base{};
        uint64_t original_spawn{}, original_destroy{}, original_seamless{};
        std::unique_ptr<PLH::x64Detour> spawn_hook, destroy_hook, seamless_hook;
        bool enabled{true};
        ReflectedPropertyCache<64> properties;
        uint64_t retained{}, reused{}, suppressed_destroy{}, native_spawns{};
        std::string error;

        struct TickState
        {
            FWeakObjectPtr object;
            bool actor{};
            bool enabled{};
        };
        struct ActorState
        {
            FWeakObjectPtr actor;
            bool hidden{}, collision{};
        };
        struct Pair
        {
            FWeakObjectPtr pawn, controller, state, world, local_player;
            std::vector<TickState> ticks;
            std::vector<ActorState> actors;
            bool parked{};
        };
        Pair standby;
        struct Operation
        {
            Pair source;
            Pair destination;
            bool controller_reused{}, pawn_reused{};
        };
        thread_local Operation* operation{};

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "character_pawn_pool", 512)
            {
            }
        };
        UObject* object(UObject* owner, const wchar_t* name)
        {
            return properties.read_object(owner, name);
        }
        bool query(UObject* owner, const wchar_t* method)
        {
            Call call(owner, method);
            call.run();
            return call.boolean_result();
        }
        void boolean(UObject* owner, const wchar_t* method, const wchar_t* parameter, bool value)
        {
            Call call(owner, method);
            call.boolean(parameter, value);
            call.run();
        }
        int player_id(UObject* pawn)
        {
            auto* field = properties.resolve(pawn, L"m_PlayerUniqueID");
            return field && field->GetElementSize() == 1
                       ? *field->ContainerPtrToValuePtr<uint8_t>(pawn)
                       : -1;
        }
        bool valid(const Pair& pair)
        {
            return pair.pawn.Get() && pair.controller.Get() && pair.state.Get() &&
                   pair.world.Get() == kh02_current_world() &&
                   kh02_actor_world(pair.pawn.Get()) == pair.world.Get() &&
                   kh02_actor_world(pair.controller.Get()) == pair.world.Get();
        }
        void collect_actor(Pair& pair, UObject* actor)
        {
            if (!is_uobject_valid_fast(actor) || kh02_actor_world(actor) != pair.world.Get() ||
                std::any_of(pair.actors.begin(), pair.actors.end(),
                            [&](const auto& entry)
                            {
                                return entry.actor.Get() == actor;
                            }))
            {
                return;
            }
            if (pair.actors.size() >= maximum_pooled_actors)
            {
                throw std::runtime_error("pool_attachment_limit");
            }
            auto* hidden = properties.resolve(actor, L"bHidden");
            if (!hidden || hidden->GetClass().GetName() != L"BoolProperty")
            {
                throw std::runtime_error("pool_hidden_layout");
            }
            pair.actors.push_back(
                {make_uobject_weak(actor),
                 static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(actor),
                 query(actor, L"GetActorEnableCollision")});
            pair.ticks.push_back(
                {make_uobject_weak(actor), true, query(actor, L"IsActorTickEnabled")});
            auto* component_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/Engine.ActorComponent");
            for (auto* component :
                 static_cast<AActor*>(actor)->K2_GetComponentsByClass(component_class))
            {
                pair.ticks.push_back({make_uobject_weak(component), false,
                                      query(component, L"IsComponentTickEnabled")});
            }
            Call attached(actor, L"GetAttachedActors");
            attached.run();
            auto* field = attached.param(L"OutActors");
            if (!field || field->GetElementSize() != sizeof(TArray<UObject*>))
            {
                throw std::runtime_error("pool_attachment_layout");
            }
            const auto& children =
                *field->ContainerPtrToValuePtr<TArray<UObject*>>(attached.parameters.data());
            if (children.Num() < 0 || children.Num() > maximum_attached_children)
            {
                throw std::runtime_error("pool_attachment_count");
            }
            for (auto* child : children)
            {
                collect_actor(pair, child);
            }
        }
        Pair capture(UObject* pawn)
        {
            Pair pair;
            pair.pawn = make_uobject_weak(pawn);
            pair.controller = make_uobject_weak(object(pawn, L"Controller"));
            pair.state = make_uobject_weak(object(pair.controller.Get(), L"PlayerState"));
            pair.local_player = make_uobject_weak(object(pair.controller.Get(), L"Player"));
            pair.world = make_uobject_weak(kh02_actor_world(pawn));
            if (!valid(pair))
            {
                throw std::runtime_error("pool_capture_lifetime");
            }
            auto* state_field = properties.resolve(pair.controller.Get(), L"PlayerState");
            if (!state_field || state_field->GetOffset_Internal() != player_state_offset ||
                state_field->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("pool_player_state_layout");
            }
            collect_actor(pair, pawn);
            collect_actor(pair, pair.controller.Get());
            collect_actor(pair, object(pair.controller.Get(), L"PlayerCameraManager"));
            collect_actor(pair, object(pair.controller.Get(), L"MyHUD"));
            collect_actor(pair, pair.state.Get());
            return pair;
        }
        void park(Pair& pair)
        {
            runtime_controller_clear_parked_input(pair.controller.Get());
            for (const auto& entry : pair.actors)
            {
                if (auto* actor = entry.actor.Get())
                {
                    boolean(actor, L"SetActorEnableCollision", L"bNewActorEnableCollision", false);
                    boolean(actor, L"SetActorHiddenInGame", L"bNewHidden", true);
                }
            }
            for (const auto& tick : pair.ticks)
            {
                if (auto* target = tick.object.Get())
                {
                    boolean(target,
                            tick.actor ? L"SetActorTickEnabled" : L"SetComponentTickEnabled",
                            L"bEnabled", false);
                }
            }
            pair.parked = true;
        }
        void wake(Pair& pair)
        {
            for (const auto& entry : pair.actors)
            {
                if (auto* actor = entry.actor.Get())
                {
                    boolean(actor, L"SetActorHiddenInGame", L"bNewHidden", entry.hidden);
                    boolean(actor, L"SetActorEnableCollision", L"bNewActorEnableCollision",
                            entry.collision);
                }
            }
            for (const auto& tick : pair.ticks)
            {
                if (auto* target = tick.object.Get())
                {
                    boolean(target,
                            tick.actor ? L"SetActorTickEnabled" : L"SetComponentTickEnabled",
                            L"bEnabled", tick.enabled);
                }
            }
            pair.parked = false;
        }
        void destroy_pair(Pair& pair)
        {
            if (!pair.parked)
            {
                pair = {};
                return;
            }
            // Never destroy the currently possessed pair, including a partial handoff.
            auto* controller = pair.controller.Get();
            if (controller && object(controller, L"Player"))
            {
                throw std::runtime_error("pool_clear_active_controller");
            }
            for (auto* actor : {pair.pawn.Get(), controller})
            {
                if (actor)
                {
                    Call destroy(actor, L"K2_DestroyActor");
                    destroy.run();
                }
            }
            pair = {};
        }
        void retire_failed_pair(Pair& pair) noexcept
        {
            // A failed transaction must not strand a second live controller.
            // Preserve any pair already adopted by the local player; retire
            // only its detached counterpart in the same surviving world.
            try
            {
                if (pair.world.Get() != kh02_current_world() ||
                    object(pair.controller.Get(), L"Player") ||
                    object(pair.pawn.Get(), L"Controller"))
                {
                    return;
                }
                pair.parked = true;
                destroy_pair(pair);
            }
            catch (...)
            {
                // Do not throw from transaction unwinding. The pool stays
                // disabled and the original failure remains visible.
            }
        }
        void place(UObject* pawn, UObject* source)
        {
            Call location(source, L"K2_GetActorLocation");
            location.run();
            Call rotation(source, L"K2_GetActorRotation");
            rotation.run();
            const auto position = location.result_vector();
            const auto angles = rotation.result_vector();
            Call move(pawn, L"K2_SetActorLocationAndRotation");
            move.vector(L"NewLocation", position[0], position[1], position[2]);
            move.vector(L"NewRotation", angles[0], angles[1], angles[2]);
            move.boolean(L"bSweep", false);
            move.boolean(L"bTeleport", true);
            move.run();
        }
        UObject* reuse_spawn(UObject* world, UObject* type, uintptr_t caller)
        {
            if (operation && world == operation->source.world.Get() &&
                valid(operation->destination))
            {
                auto* cached = caller == controller_spawn_return
                                   ? operation->destination.controller.Get()
                               : caller == pawn_spawn_return ? operation->destination.pawn.Get()
                                                             : nullptr;
                if (cached && cached->GetClassPrivate() == type)
                {
                    // Retained actors remain in their level, but only active
                    // pawns/controllers belong in the world's gameplay lists.
                    // Recreate the registration normally performed by spawning.
                    if (caller == controller_spawn_return)
                    {
                        reinterpret_cast<WorldActorRegistration>(base + add_controller_rva)(world,
                                                                                            cached);
                        operation->controller_reused = true;
                    }
                    else
                    {
                        reinterpret_cast<WorldActorRegistration>(base + add_pawn_rva)(world,
                                                                                      cached);
                        operation->pawn_reused = true;
                    }
                    ++reused;
                    return cached;
                }
            }
            if (operation && (caller == controller_spawn_return || caller == pawn_spawn_return))
            {
                ++native_spawns;
            }
            return nullptr;
        }
        UObject* spawn(UObject* world, UObject* type, const void* transform, const void* parameters)
        {
            if (auto* cached = reuse_spawn(world, type, reinterpret_cast<uintptr_t>(_ReturnAddress()) - base))
            {
                return cached;
            }
            return reinterpret_cast<Spawn>(original_spawn)(world, type, transform, parameters);
        }
        UObject* spawn_steam(UObject* world, UObject* type, const void* location, const void* rotation,
                             const void* parameters)
        {
            if (auto* cached = reuse_spawn(world, type, reinterpret_cast<uintptr_t>(_ReturnAddress()) - base))
            {
                return cached;
            }
            // Steam's factory calls the location/rotation overload, whose fifth
            // argument is stack-passed. Keep its native transform construction.
            using SpawnSteam = UObject* (*)(UObject*, UObject*, const void*, const void*, const void*);
            return reinterpret_cast<SpawnSteam>(original_spawn)(world, type, location, rotation, parameters);
        }
        bool destroy(UObject* world, UObject* actor, bool net_force, bool modify_level)
        {
            if (operation && world == operation->source.world.Get() &&
                (actor == operation->source.pawn.Get() ||
                 actor == operation->source.controller.Get() ||
                 actor == operation->source.state.Get()))
            {
                // KH3's UI asks UWorld::GetFirstPlayerController every tick.
                // Merely hiding the retired controller leaves it first in that
                // list. Preserve destruction's unregister step without deleting
                // the actor or its components.
                if (actor == operation->source.controller.Get())
                {
                    reinterpret_cast<WorldActorRegistration>(base + remove_controller_rva)(world,
                                                                                           actor);
                }
                else if (actor == operation->source.pawn.Get())
                {
                    reinterpret_cast<WorldActorRegistration>(base + remove_pawn_rva)(world, actor);
                }
                ++suppressed_destroy;
                return true;
            }
            return reinterpret_cast<Destroy>(original_destroy)(world, actor, net_force,
                                                               modify_level);
        }
        void seamless_from(UObject* incoming, UObject* outgoing)
        {
            auto* saved = operation && outgoing == operation->source.controller.Get()
                              ? operation->source.state.Get()
                              : nullptr;
            reinterpret_cast<SeamlessFrom>(original_seamless)(incoming, outgoing);
            if (saved)
            {
                // Native SeamlessTravelFrom clears the old pointer after retiring
                // PlayerState. Its exact retirement was suppressed above.
                std::memcpy(reinterpret_cast<std::byte*>(outgoing) + player_state_offset, &saved,
                            sizeof(saved));
            }
        }
        bool install()
        {
            if (spawn_hook && destroy_hook && seamless_hook)
            {
                return true;
            }
            base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!character_native::verify_sites({character_native::Site::pool_spawn, character_native::Site::pool_destroy, character_native::Site::pool_seamless_from, character_native::Site::pool_add_controller, character_native::Site::pool_remove_controller, character_native::Site::pool_add_pawn, character_native::Site::pool_remove_pawn, character_native::Site::pool_controller_spawn_call, character_native::Site::pool_pawn_spawn_call}))
            {
                return false;
            }
            spawn_hook = std::make_unique<PLH::x64Detour>(
                base + spawn_rva, character_native::build() == character_native::Build::Steam
                                      ? reinterpret_cast<uintptr_t>(&spawn_steam)
                                      : reinterpret_cast<uintptr_t>(&spawn), &original_spawn);
            destroy_hook = std::make_unique<PLH::x64Detour>(
                base + destroy_rva, reinterpret_cast<uintptr_t>(&destroy), &original_destroy);
            seamless_hook = std::make_unique<PLH::x64Detour>(
                base + seamless_from_rva, reinterpret_cast<uintptr_t>(&seamless_from),
                &original_seamless);
            if (!spawn_hook->hook() || !destroy_hook->hook() || !seamless_hook->hook())
            {
                seamless_hook.reset();
                destroy_hook.reset();
                spawn_hook.reset();
                return false;
            }
            return true;
        }
    } // namespace
    struct Transaction::Impl
    {
        Operation operation;
        bool finished{};
    };
    Transaction::Transaction(UObject* outgoing, int target)
    {
        if (!enabled || kh02_current_story_world())
        {
            return;
        }
        if (operation)
        {
            throw std::runtime_error("pool_nested_transaction");
        }
        // Install only when a swap needs reuse, including ordinary builds
        // without the diagnostic toggle. Unsupported hooks keep native creation.
        if (!install())
        {
            enabled = false;
            error = "pool_hook_install_failed";
            return;
        }
        impl_ = std::make_unique<Impl>();
        impl_->operation.source = capture(outgoing);
        if (valid(standby) && player_id(standby.pawn.Get()) == target)
        {
            impl_->operation.destination = std::move(standby);
            standby = {};
            place(impl_->operation.destination.pawn.Get(), outgoing);
            wake(impl_->operation.destination);
        }
        else
        {
            destroy_pair(standby);
        }
        operation = &impl_->operation;
    }
    Transaction::~Transaction()
    {
        if (!impl_)
        {
            return;
        }
        operation = nullptr;
        if (!impl_->finished)
        {
            enabled = false;
            error = "incomplete_pool_transaction";
            retire_failed_pair(impl_->operation.source);
            retire_failed_pair(impl_->operation.destination);
        }
    }
    void Transaction::finish()
    {
        if (!impl_)
        {
            return;
        }
        operation = nullptr;
        auto& active = impl_->operation;
        auto* incoming_controller = object(active.source.local_player.Get(), L"PlayerController");
        auto* incoming = object(incoming_controller, L"Pawn");
        if (!valid(active.source) || !is_uobject_valid_fast(incoming) ||
            incoming == active.source.pawn.Get() ||
            kh02_actor_world(incoming) != active.source.world.Get() ||
            object(active.source.controller.Get(), L"Player") ||
            object(active.source.pawn.Get(), L"Controller"))
        {
            throw std::runtime_error("pool_source_not_detached");
        }
        if (active.destination.pawn.Get() && (!active.controller_reused || !active.pawn_reused ||
                                              incoming != active.destination.pawn.Get()))
        {
            throw std::runtime_error("pool_reuse_incomplete");
        }
        park(active.source);
        // A parked pawn does not tick its status timer. The swap session owns
        // the active HP Break now, so reusing this pawn cannot revive an old cap.
        character_swap::CharacterSwapHpBreak::clear_parked(active.source.pawn.Get());
        standby = std::move(active.source);
        ++retained;
        impl_->finished = true;
    }
    void clear()
    {
        if (operation)
        {
            return;
        }
        destroy_pair(standby);
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool set_enabled(bool value)
    {
        if (operation)
        {
            return false;
        }
        if (!value)
        {
            clear();
        }
        if (value && !install())
        {
            return false;
        }
        enabled = value;
        error.clear();
        return true;
    }
    nlohmann::json snapshot()
    {
        const auto name = [](UObject* value)
        {
            return value ? to_utf8(value->GetName()) : std::string{};
        };
        size_t ticking{}, visible{}, collidable{}, live_actors{}, live_ticks{};
        for (const auto& tick : standby.ticks)
        {
            if (auto* target = tick.object.Get())
            {
                ++live_ticks;
                if (query(target, tick.actor ? L"IsActorTickEnabled" : L"IsComponentTickEnabled"))
                {
                    ++ticking;
                }
            }
        }
        for (const auto& entry : standby.actors)
        {
            if (auto* actor = entry.actor.Get())
            {
                ++live_actors;
                auto* hidden = static_cast<FBoolProperty*>(properties.resolve(actor, L"bHidden"));
                if (!hidden->GetPropertyValueInContainer(actor))
                {
                    ++visible;
                }
                if (query(actor, L"GetActorEnableCollision"))
                {
                    ++collidable;
                }
            }
        }
        return {{"enabled", enabled},
                {"retained", retained},
                {"reused_actors", reused},
                {"native_spawns", native_spawns},
                {"suppressed_destroy", suppressed_destroy},
                {"parked", standby.parked},
                {"parked_id", player_id(standby.pawn.Get())},
                {"parked_pawn", name(standby.pawn.Get())},
                {"parked_controller", name(standby.controller.Get())},
                {"parked_actors", live_actors},
                {"parked_tick_functions", live_ticks},
                {"parked_ticking", ticking},
                {"parked_visible", visible},
                {"parked_collidable", collidable},
                {"error", error}};
    }
#endif
} // namespace kh3ap::character_pawn_pool
