#include "betrayal_trap.h"
#include "betrayal_target_hooks.h"
#include "party_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/colors/color_events.h"
#include "kh3ap/features/player/player_ids.h"
#include <Unreal/AActor.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include "kh3ap/features/player/player_state_ids.h"
#include <cstring>
#include <stdexcept>
#include <optional>
#include <vector>
#include <Windows.h>
#include <atomic>

namespace kh3ap::betrayal_trap
{
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto duration = std::chrono::seconds(15);
        constexpr int enemy_team = 0x20;
        struct Patch
        {
            FWeakObjectPtr owner;
            std::wstring name;
            uint64_t original{};
            int size{};
            bool boolean{};
            bool collision_team{};
        };
        struct Companion
        {
            FWeakObjectPtr pawn;
            FWeakObjectPtr controller;
            FWeakObjectPtr board;
            FWeakObjectPtr original_target;
            FWeakObjectPtr original_board_target;
        };
        struct IgnoredCompanions
        {
            FWeakObjectPtr collision;
            std::vector<FWeakObjectPtr> added;
        };
        struct ActionTarget
        {
            FWeakObjectPtr state;
            std::wstring name;
            FWeakObjectPtr original;
        };
        struct Projectile
        {
            FWeakObjectPtr actor;
            Clock::time_point deadline;
            bool owned{};
        };
        struct TrapState
        {
            FWeakObjectPtr player;
            FWeakObjectPtr world;
            Clock::time_point expires{};
            bool running{};
            bool swap_pending{};
            Clock::time_point swap_deadline{};
            std::vector<Patch> patches;
            std::vector<Companion> companions;
            std::vector<IgnoredCompanions> ignored_companions;
            std::vector<ActionTarget> action_targets;
            std::vector<Projectile> projectiles;
            FWeakObjectPtr projectile_class;
            std::atomic<DWORD> active_thread{};
            uint64_t equipment_generation{};
            uint64_t player_handoffs{};
            uint64_t projectile_count{};
            uint64_t target_redirects{};
            std::string last_result = "idle";
        };
        TrapState trap;
        // Reflection and installed hooks are shared across effect activations.
        ReflectedPropertyCache<128> properties;
        std::array<bool, 3> hooks_installed{};

        FProperty* field(UObject* object, const wchar_t* name, const wchar_t* kind, int size)
        {
            auto* result = properties.resolve(object, name);
            if (!result || result->GetClass().GetName() != kind || result->GetElementSize() != size)
            {
                throw std::runtime_error("betrayal_property_layout");
            }
            return result;
        }
        void scalar(UObject* object, const wchar_t* name, const wchar_t* kind, int size,
                    uint64_t value, std::optional<uint64_t> restore_value = std::nullopt)
        {
            auto* property = field(object, name, kind, size);
            auto* address = property->ContainerPtrToValuePtr<void>(object);
            const auto saved = std::find_if(trap.patches.begin(), trap.patches.end(),
                                            [&](const Patch& patch)
                                            {
                                                return patch.owner.Get() == object &&
                                                       !patch.boolean && !patch.collision_team &&
                                                       patch.name == name;
                                            });
            if (saved == trap.patches.end())
            {
                if (trap.patches.size() >= 256)
                {
                    throw std::runtime_error("betrayal_patch_limit");
                }
                Patch patch{make_uobject_weak(object), name, 0, size};
                std::memcpy(&patch.original, address, size);
                if (restore_value)
                {
                    patch.original = *restore_value;
                }
                trap.patches.push_back(patch);
            }
            std::memcpy(address, &value, size);
        }
        void flag(UObject* object, const wchar_t* name, bool value)
        {
            auto* property = properties.resolve(object, name);
            if (!property || property->GetClass().GetName() != L"BoolProperty")
            {
                throw std::runtime_error("betrayal_bool_layout");
            }
            auto* boolean = static_cast<FBoolProperty*>(property);
            if (std::none_of(trap.patches.begin(), trap.patches.end(),
                             [&](const Patch& patch)
                             {
                                 return patch.boolean && patch.owner.Get() == object &&
                                        patch.name == name;
                             }))
            {
                if (trap.patches.size() >= 256)
                {
                    throw std::runtime_error("betrayal_patch_limit");
                }
                trap.patches.push_back({make_uobject_weak(object), name,
                                        boolean->GetPropertyValueInContainer(object), 0, true});
            }
            boolean->SetPropertyValueInContainer(object, value);
        }
        int* collision_team(UObject* collision)
        {
            // The native collision team precedes the reflected ignored-actor array.
            auto* ignored = field(collision, L"m_IgnoreActors", L"ArrayProperty", 16);
            if (ignored->GetOffset_Internal() != 0x870)
            {
                throw std::runtime_error("betrayal_collision_team_layout");
            }
            return reinterpret_cast<int*>(static_cast<std::byte*>(static_cast<void*>(collision)) +
                                          0x868);
        }
        void change_collision_team(UObject* collision,
                                   std::optional<uint64_t> restore_value = std::nullopt)
        {
            auto* team = collision_team(collision);
            const auto saved =
                std::find_if(trap.patches.begin(), trap.patches.end(),
                             [&](const Patch& patch)
                             {
                                 return patch.collision_team && patch.owner.Get() == collision;
                             });
            if (saved == trap.patches.end())
            {
                if (trap.patches.size() >= 256)
                {
                    throw std::runtime_error("betrayal_patch_limit");
                }
                trap.patches.push_back({make_uobject_weak(collision), L"",
                                        restore_value.value_or(static_cast<uint64_t>(*team)), 4,
                                        false, true});
            }
            *team = enemy_team;
        }
        std::vector<UObject*> equipped_weapons(UObject* pawn)
        {
            auto* equipment = properties.read_object(pawn, L"MyEquipment");
            if (!equipment)
            {
                return {};
            }
            auto* property = field(equipment, L"m_WeaponEquips", L"ArrayProperty", 16);
            auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("betrayal_weapon_array_layout");
            }
            auto* weapons = property->ContainerPtrToValuePtr<TArray<UObject*>>(equipment);
            if (weapons->Num() < 0 || weapons->Num() > 16)
            {
                throw std::runtime_error("betrayal_weapon_array_range");
            }
            std::vector<UObject*> result;
            for (auto* weapon : *weapons)
            {
                if (!weapon)
                {
                    continue;
                }
                if (!is_uobject_valid_fast(weapon) ||
                    properties.read_object(weapon, L"MyPawn") != pawn ||
                    kh02_actor_world(weapon) != kh02_actor_world(pawn))
                {
                    throw std::runtime_error("betrayal_weapon_owner_mismatch");
                }
                result.push_back(weapon);
            }
            return result;
        }
        TArray<UObject*>* ignored_actors(UObject* collision)
        {
            auto* property = field(collision, L"m_IgnoreActors", L"ArrayProperty", 16);
            auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("betrayal_ignore_array_layout");
            }
            auto* actors = property->ContainerPtrToValuePtr<TArray<UObject*>>(collision);
            if (actors->Num() < 0 || actors->Num() > 64 || actors->Max() < actors->Num() ||
                (actors->Num() && !actors->GetData()))
            {
                throw std::runtime_error("betrayal_ignore_array_range");
            }
            return actors;
        }
        void ignore_companions(UObject* collision)
        {
            auto* actors = ignored_actors(collision);
            auto found =
                std::find_if(trap.ignored_companions.begin(), trap.ignored_companions.end(),
                             [collision](const auto& entry)
                             {
                                 return entry.collision.Get() == collision;
                             });
            if (found == trap.ignored_companions.end())
            {
                if (trap.ignored_companions.size() >= 128)
                {
                    throw std::runtime_error("betrayal_ignore_limit");
                }
                trap.ignored_companions.push_back({make_uobject_weak(collision), {}});
                found = std::prev(trap.ignored_companions.end());
            }
            for (const auto& companion : trap.companions)
            {
                auto* pawn = companion.pawn.Get();
                if (!pawn || std::find(actors->begin(), actors->end(), pawn) != actors->end())
                {
                    continue;
                }
                found->added.push_back(make_uobject_weak(pawn));
                actors->Add(pawn);
            }
        }
        void ignore_companion_hits(UObject* pawn)
        {
            ignore_companions(properties.read_object(pawn, L"MyAtkColl"));
            for (auto* weapon : equipped_weapons(pawn))
            {
                ignore_companions(properties.read_object(weapon, L"MyAtkColl"));
            }
        }
        void change_weapon_teams(UObject* pawn)
        {
            std::optional<uint64_t> original_team, original_collision_team;
            if (trap.running)
            {
                auto* collision = properties.read_object(pawn, L"MyAtkColl");
                for (const auto& patch : trap.patches)
                {
                    if (patch.owner.Get() == pawn && patch.name == L"m_TeamNo")
                    {
                        original_team = patch.original;
                    }
                    if (patch.owner.Get() == collision && patch.collision_team)
                    {
                        original_collision_team = patch.original;
                    }
                }
                if (!original_team || !original_collision_team)
                {
                    throw std::runtime_error("betrayal_weapon_original_team_missing");
                }
            }
            for (auto* weapon : equipped_weapons(pawn))
            {
                // New weapons may inherit the temporarily hostile pawn's team.
                // Existing snapshots retain their own pre-trap values.
                scalar(weapon, L"m_TeamNo", L"IntProperty", 4, enemy_team, original_team);
                change_collision_team(properties.read_object(weapon, L"MyAtkColl"),
                                      original_collision_team);
            }
        }
        UObject* read_controller_target(UObject* controller)
        {
            Kh02ReflectedCall call(controller, L"GetMyTarget", "betrayal controller target", 64);
            call.run();
            return call.result();
        }

        UObject* read_blackboard_target(UObject* board)
        {
            Kh02ReflectedCall call(board, L"GetValueAsObject", "betrayal blackboard target", 64);
            call.name(L"KeyName", L"TargetActor");
            call.run();
            return call.result();
        }

        UObject* current_action(UObject* pawn)
        {
            return properties.read_object(properties.read_object(pawn, L"MyStateComp"),
                                          L"m_pCurrentState");
        }

        void set_companion_targets(const Companion& companion, UObject* target,
                                   UObject* board_target)
        {
            if (auto* controller = companion.controller.Get())
            {
                Kh02ReflectedCall call(controller, L"SetMyTarget", "betrayal target", 64);
                call.object(L"inNewMyTarget", target);
                call.run();
            }
            if (auto* board = companion.board.Get())
            {
                Kh02ReflectedCall call(board, L"SetValueAsObject", "betrayal blackboard", 64);
                call.name(L"KeyName", L"TargetActor");
                call.object(L"ObjectValue", board_target);
                call.run();
            }
        }
        bool owns(UObject* pawn)
        {
            if (!trap.running)
            {
                return false;
            }
            for (const auto& companion : trap.companions)
            {
                if (companion.pawn.Get() == pawn)
                {
                    return true;
                }
            }
            return false;
        }
        FWeakObjectPtr* weak_target(UObject* state, const wchar_t* name)
        {
            auto* property = properties.resolve(state, name);
            return property && property->GetClass().GetName() == L"WeakObjectProperty" &&
                           property->GetElementSize() == sizeof(FWeakObjectPtr)
                       ? property->ContainerPtrToValuePtr<FWeakObjectPtr>(state)
                       : nullptr;
        }
        void retarget_action(UObject* state, UObject* target)
        {
            if (!owns(properties.read_object(state, L"MyCharPawn")))
            {
                return;
            }
            for (const auto* name : {L"m_Target", L"m_LockonTarget"})
            {
                auto* value = weak_target(state, name);
                if (!value || !value->Get())
                {
                    continue;
                }
                const auto found =
                    std::find_if(trap.action_targets.begin(), trap.action_targets.end(),
                                 [&](const ActionTarget& saved)
                                 {
                                     return saved.state.Get() == state && saved.name == name;
                                 });
                if (found == trap.action_targets.end())
                {
                    if (trap.action_targets.size() >= 256)
                    {
                        throw std::runtime_error("betrayal_action_limit");
                    }
                    // A trap-created target must not survive the effect's restoration.
                    auto* original = value->Get();
                    if (original == trap.player.Get() ||
                        original == properties.read_object(trap.player.Get(), L"m_FlowTarget"))
                    {
                        original = nullptr;
                    }
                    trap.action_targets.push_back(
                        {make_uobject_weak(state), name, make_uobject_weak(original)});
                }
                auto* destination = std::wstring_view(name) == L"m_Target"
                                        ? target
                                        : properties.read_object(target, L"m_FlowTarget");
                *value = make_uobject_weak(destination);
            }
        }
        void retarget_companion(const Companion& companion, UObject* target)
        {
            set_companion_targets(companion, target, target);
            retarget_action(current_action(companion.pawn.Get()), target);
        }

        void retarget_projectile(UObject* actor, UObject* target)
        {
            flag(actor, L"m_bAutoTarget", false);
            ignore_companions(properties.read_object(actor, L"MyAtkColl"));
            auto* movement = properties.read_object(actor, L"MyMovement");
            if (movement && properties.read_object(movement, L"m_HomingTargetActor") != target)
            {
                Kh02ReflectedCall call(actor, L"BP_SetHomingTarget", "betrayal projectile target",
                                       64);
                call.object(L"InTarget", target);
                call.run();
            }
        }
        void update_projectiles(UObject* target, Clock::time_point now)
        {
            // Construction queues only game-thread projectile instances while active.
            // Ownership is assigned after construction, so retry pending instances briefly.
            for (auto it = trap.projectiles.begin(); it != trap.projectiles.end();)
            {
                auto* actor = it->actor.Get();
                if (!actor || kh02_actor_world(actor) != trap.world.Get())
                {
                    it = trap.projectiles.erase(it);
                    continue;
                }
                if (!it->owned)
                {
                    Kh02ReflectedCall owner(actor, L"GetBattleOwner", "betrayal projectile owner",
                                            64);
                    owner.run();
                    auto* source = owner.result();
                    it->owned = owns(source) || owns(properties.read_object(source, L"MyPawn")) ||
                                owns(properties.read_object(actor, L"Instigator"));
                    if (it->owned)
                    {
                        ++trap.projectile_count;
                    }
                    else if (now >= it->deadline)
                    {
                        it = trap.projectiles.erase(it);
                        continue;
                    }
                }
                if (it->owned)
                {
                    retarget_projectile(actor, target);
                }
                ++it;
            }
        }
        UObject* native_target(UObject* controller)
        {
            if (!trap.running)
            {
                return nullptr;
            }
            auto* target = trap.player.Get();
            auto* active_world = trap.world.Get();
            if (!target || !active_world || active_world != kh02_current_world() ||
                kh02_actor_world(target) != active_world)
            {
                return nullptr;
            }
            auto* player_controller = properties.read_object(target, L"Controller");
            if (!player_controller || properties.read_object(player_controller, L"Pawn") != target)
            {
                return nullptr;
            }
            for (const auto& companion : trap.companions)
            {
                if (companion.controller.Get() != controller)
                {
                    continue;
                }
                auto* pawn = companion.pawn.Get();
                if (pawn && properties.read_object(controller, L"Pawn") == pawn &&
                    properties.read_object(pawn, L"Controller") == controller &&
                    kh02_actor_world(pawn) == active_world)
                {
                    ++trap.target_redirects;
                    return target;
                }
            }
            return nullptr;
        }
        void install_hooks()
        {
            size_t index = 0;
            for (const auto* path : {L"/Script/TresGame.TresNpcPawnBase:CanBattle",
                                     L"/Script/TresGame.TresNpcPawnBase:IsHostileTarget",
                                     L"/Script/TresGame.TresAIPawnBase:IsHostileTarget"})
            {
                const auto hook_index = index++;
                if (hooks_installed[hook_index])
                {
                    continue;
                }
                auto* function =
                    UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
                if (!is_uobject_valid_fast(function) || function->GetParmsSize() != 1)
                {
                    throw std::runtime_error("betrayal_ai_function_layout");
                }
                const auto ids = UObjectGlobals::RegisterHook(
                    function,
                    [](auto&, void*)
                    {
                    },
                    [](UnrealScriptFunctionCallableContext& context, void*)
                    {
                        if (owns(context.Context) && context.RESULT_DECL)
                        {
                            *static_cast<uint8_t*>(context.RESULT_DECL) = 1;
                        }
                    },
                    nullptr);
                hooks_installed[hook_index] = ids.first != 0 || ids.second != 0;
                if (!hooks_installed[hook_index])
                {
                    throw std::runtime_error("betrayal_ai_hook_failed");
                }
            }
        }

        void restore()
        {
            trap.active_thread.store(0, std::memory_order_release);
            trap.running = false;
            trap.swap_pending = false;
            // End hostile projectiles without their impact/timeout explosion events.
            for (const auto& projectile : trap.projectiles)
            {
                if (auto* actor = projectile.actor.Get(); actor && projectile.owned)
                {
                    try
                    {
                        Kh02ReflectedCall destroy(actor, L"K2_DestroyActor",
                                                  "betrayal projectile cleanup", 64);
                        destroy.run();
                    }
                    catch (...)
                    {
                        trap.last_result = "projectile_cleanup_failed";
                    }
                }
            }
            trap.projectiles.clear();
            trap.projectile_class = {};
            for (const auto& saved : trap.action_targets)
            {
                if (auto* value = weak_target(saved.state.Get(), saved.name.c_str()))
                {
                    *value = saved.original;
                }
            }
            trap.action_targets.clear();
            for (const auto& entry : trap.ignored_companions)
            {
                if (auto* collision = entry.collision.Get())
                {
                    try
                    {
                        auto* actors = ignored_actors(collision);
                        for (const auto& added : entry.added)
                        {
                            if (auto* pawn = added.Get())
                            {
                                for (int i = actors->Num() - 1; i >= 0; --i)
                                {
                                    if ((*actors)[i] == pawn)
                                    {
                                        actors->RemoveAt(i);
                                    }
                                }
                            }
                        }
                    }
                    catch (...)
                    {
                        trap.last_result = "ignore_restore_failed";
                    }
                }
            }
            trap.ignored_companions.clear();
            for (auto it = trap.patches.rbegin(); it != trap.patches.rend(); ++it)
            {
                auto* owner = it->owner.Get();
                if (!owner)
                {
                    continue;
                }
                try
                {
                    if (it->collision_team)
                    {
                        *collision_team(owner) = static_cast<int>(it->original);
                    }
                    else if (it->boolean)
                    {
                        auto* property = properties.resolve(owner, it->name);
                        if (property && property->GetClass().GetName() == L"BoolProperty")
                        {
                            static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(
                                owner, it->original != 0);
                        }
                    }
                    else if (auto* property = properties.resolve(owner, it->name);
                             property && property->GetElementSize() == it->size)
                    {
                        std::memcpy(property->ContainerPtrToValuePtr<void>(owner), &it->original,
                                    it->size);
                    }
                }
                catch (const std::exception& error)
                {
                    RC::Output::send<RC::LogLevel::Error>(
                        L"[KH3APProbeCpp] Betrayal Trap restoration failed: {}\n",
                        std::wstring(error.what(), error.what() + std::strlen(error.what())));
                }
            }
            for (const auto& companion : trap.companions)
            {
                auto* pawn = companion.pawn.Get();
                auto* controller = companion.controller.Get();
                if (!pawn || !controller ||
                    properties.read_object(pawn, L"Controller") != controller ||
                    properties.read_object(controller, L"Pawn") != pawn)
                {
                    continue;
                }
                try
                {
                    set_companion_targets(companion, companion.original_target.Get(),
                                          companion.original_board_target.Get());
                }
                catch (...)
                {
                    trap.last_result = "target_restore_failed";
                }
            }
            trap.patches.clear();
            trap.companions.clear();
            trap.player = {};
            trap.world = {};
        }
    } // namespace

    bool active()
    {
        return trap.running;
    }

    bool start(UObject* controlled_player)
    {
        if (trap.running || !is_uobject_valid_fast(controlled_player) ||
            character_swap::manual_swaps_blocked() || kh02_current_story_world() ||
            !runtime_trap_application_state_status(controlled_player).ready)
        {
            return false;
        }
        try
        {
            auto* player_id = properties.resolve(controlled_player, L"m_PlayerUniqueID");
            auto* controller = properties.read_object(controlled_player, L"Controller");
            if (!player_id || player_id->GetElementSize() != sizeof(uint8_t) ||
                (player_id->GetClass().GetName() != L"EnumProperty" &&
                 player_id->GetClass().GetName() != L"ByteProperty") ||
                (!is_standard_sora_player_id(
                     *player_id->ContainerPtrToValuePtr<uint8_t>(controlled_player)) &&
                 character_swap::game_over_context() != controlled_player) ||
                !controller || properties.read_object(controller, L"Pawn") != controlled_player)
            {
                return false;
            }
            install_hooks();
            auto* current_world = kh02_actor_world(controlled_player);
            if (!current_world || current_world != kh02_current_world())
            {
                return false;
            }
            std::array<UObject*, 6> slots{};
            if (!party_native::read_friend_slots(
                    properties.read_object(controlled_player, L"MyFrdMgr"), slots))
            {
                return false;
            }
            for (auto* pawn : slots)
            {
                if (!pawn || pawn == controlled_player || kh02_actor_world(pawn) != current_world)
                {
                    continue;
                }
                auto* controller = properties.read_object(pawn, L"Controller");
                auto* board = properties.read_object(controller, L"Blackboard");
                if (!controller || !board)
                {
                    throw std::runtime_error("betrayal_companion_ai_missing");
                }
                betrayal_target_hooks::install(controller, &native_target);
                auto* original_target = read_controller_target(controller);
                auto* original_board_target = read_blackboard_target(board);
                trap.companions.push_back({make_uobject_weak(pawn), make_uobject_weak(controller),
                                           make_uobject_weak(board),
                                           make_uobject_weak(original_target),
                                           make_uobject_weak(original_board_target)});
                scalar(pawn, L"m_TeamNo", L"IntProperty", 4, enemy_team);
                scalar(pawn, L"MyTeam", L"ByteProperty", 1, enemy_team);
                flag(pawn, L"m_bNoHpDamage", false);
                auto* collision = properties.read_object(pawn, L"MyAtkColl");
                change_collision_team(collision);
                change_weapon_teams(pawn);
                auto* lock_class = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, L"/Script/TresGame.TresLockonTargetComponent");
                if (!is_uobject_valid_fast(lock_class))
                {
                    throw std::runtime_error("betrayal_lockon_class_missing");
                }
                for (auto* component :
                     static_cast<AActor*>(pawn)->K2_GetComponentsByClass(lock_class))
                {
                    scalar(component, L"m_TeamNo", L"IntProperty", 4, enemy_team);
                    flag(component, L"m_bPermissionLockOn", true);
                }
            }
            if (trap.companions.empty())
            {
                trap.last_result = "waiting_for_companions";
                return false;
            }
            trap.player = make_uobject_weak(controlled_player);
            trap.world = make_uobject_weak(current_world);
            trap.expires = Clock::now() + duration;
            trap.equipment_generation = color_equipment_generation.load(std::memory_order_acquire);
            trap.running = true;
            trap.target_redirects = 0;
            trap.player_handoffs = 0;
            trap.projectile_count = 0;
            trap.projectiles.reserve(128);
            trap.projectile_class = make_uobject_weak(UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresProjectileBase"));
            if (!trap.projectile_class.Get())
            {
                throw std::runtime_error("betrayal_projectile_class_missing");
            }
            trap.active_thread.store(GetCurrentThreadId(), std::memory_order_release);
            for (const auto& companion : trap.companions)
            {
                ignore_companion_hits(companion.pawn.Get());
                set_companion_targets(companion, controlled_player, controlled_player);
            }
            trap.last_result = "active";
            RC::Output::send<RC::LogLevel::Normal>(
                L"[KH3APProbeCpp] Betrayal Trap started companions={} duration=15\n",
                trap.companions.size());
            return true;
        }
        catch (const std::exception& error)
        {
            const bool report = trap.last_result != error.what();
            trap.last_result = error.what();
            restore();
            if (report)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    L"[KH3APProbeCpp] Betrayal Trap deferred: {}\n",
                    std::wstring(error.what(), error.what() + std::strlen(error.what())));
            }
            return false;
        }
    }

    void cancel()
    {
        if (!trap.running)
        {
            return;
        }
        trap.last_result = "cancelled";
        restore();
    }

    void prepare_player_swap(UObject* outgoing)
    {
        if (trap.running && trap.player.Get() == outgoing &&
            trap.world.Get() == kh02_current_world())
        {
            trap.swap_pending = true;
            trap.swap_deadline = Clock::now() + std::chrono::seconds(3);
        }
    }

    void handoff_player(UObject* incoming)
    {
        if (!trap.running || !trap.swap_pending || !incoming || incoming == trap.player.Get() ||
            !character_swap::owns_swap_pawn(incoming) ||
            kh02_actor_world(incoming) != trap.world.Get())
        {
            return;
        }
        auto* controller = properties.read_object(incoming, L"Controller");
        if (!controller || properties.read_object(controller, L"Pawn") != incoming)
        {
            return;
        }
        trap.player = make_uobject_weak(incoming);
        trap.swap_pending = false;
        ++trap.player_handoffs;
        for (const auto& companion : trap.companions)
        {
            retarget_companion(companion, incoming);
        }
        update_projectiles(incoming, Clock::now());
    }

    void notify_player_started()
    {
        if (!trap.swap_pending)
        {
            cancel();
        }
    }

    void observe_constructed_object(UObject* object)
    {
        if (trap.active_thread.load(std::memory_order_acquire) != GetCurrentThreadId())
        {
            return;
        }
        auto* type = static_cast<UClass*>(trap.projectile_class.Get());
        if (type && is_uobject_valid_fast(object) && object->IsA(type) &&
            trap.projectiles.size() < 128)
        {
            trap.projectiles.push_back(
                {make_uobject_weak(object), Clock::now() + std::chrono::seconds(1)});
        }
    }

    void observe_state(uintptr_t component, uintptr_t state)
    {
        if (trap.running)
        {
            try
            {
                retarget_action(reinterpret_cast<UObject*>(state), trap.player.Get());
            }
            catch (...)
            {
                cancel();
            }
        }
        if (!trap.running || properties.read_object(trap.player.Get(), L"MyStateComp") !=
                                 reinterpret_cast<UObject*>(component))
        {
            return;
        }
        auto* current = reinterpret_cast<UObject*>(state);
        auto* id = properties.resolve(current, L"MyStateID");
        if (id && id->GetElementSize() == 1 &&
            *id->ContainerPtrToValuePtr<uint8_t>(current) == player_state_ids::game_over)
        {
            cancel();
        }
    }

    void tick(Clock::time_point now)
    {
        if (!trap.running)
        {
            return;
        }
        if (trap.swap_pending && now < trap.swap_deadline && now < trap.expires &&
            trap.world.Get() == kh02_current_world())
        {
            handoff_player(character_swap::party_owner());
            if (trap.swap_pending)
            {
                return;
            }
        }
        if (trap.swap_pending)
        {
            cancel();
            return;
        }
        auto* controlled_player = trap.player.Get();
        if (now >= trap.expires || !controlled_player || trap.world.Get() != kh02_current_world() ||
            kh02_actor_world(controlled_player) != trap.world.Get())
        {
            trap.last_result = now >= trap.expires ? "completed" : "context_changed";
            restore();
            RC::Output::send<RC::LogLevel::Normal>(L"[KH3APProbeCpp] Betrayal Trap ended\n");
            return;
        }
        try
        {
            auto* controller = properties.read_object(controlled_player, L"Controller");
            if (!controller || properties.read_object(controller, L"Pawn") != controlled_player)
            {
                cancel();
                return;
            }
            const auto current_equipment_generation =
                color_equipment_generation.load(std::memory_order_acquire);
            const bool equipment_changed =
                current_equipment_generation != trap.equipment_generation;
            trap.equipment_generation = current_equipment_generation;
            for (const auto& companion : trap.companions)
            {
                auto* pawn = companion.pawn.Get();
                if (!pawn || kh02_actor_world(pawn) != trap.world.Get() ||
                    properties.read_object(pawn, L"Controller") != companion.controller.Get())
                {
                    cancel();
                    return;
                }
                // Reuse the equipment rebuild revision; weapon discovery runs only
                // at activation or when the native spawn/reuse operation fires.
                if (equipment_changed)
                {
                    change_weapon_teams(pawn);
                    ignore_companion_hits(pawn);
                }
                // Native companion trees refresh their targets during active combat.
                // Keep the override on the active frame path; idle traps do no work.
                retarget_companion(companion, controlled_player);
            }
            update_projectiles(controlled_player, now);
        }
        catch (...)
        {
            cancel();
        }
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot(UObject* controlled_player)
    {
        nlohmann::json result{
            {"active", trap.running},
            {"result", trap.last_result},
            {"target_redirects", trap.target_redirects},
            {"player_handoffs", trap.player_handoffs},
            {"swap_pending", trap.swap_pending},
            {"projectile_count", trap.projectile_count},
            {"projectiles", nlohmann::json::array()},
            {"remaining_ms",
             trap.running
                 ? std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                            trap.expires - Clock::now())
                                            .count())
                 : 0},
            {"companions", nlohmann::json::array()}};
        std::array<UObject*, 6> slots{};
        party_native::read_friend_slots(properties.read_object(controlled_player, L"MyFrdMgr"),
                                        slots);
        const auto describe = [](UObject* object)
        {
            const auto text = object ? object->GetFullName() : L"";
            return std::string(text.begin(), text.end());
        };
        const auto hp = [](UObject* pawn)
        {
            Kh02ReflectedCall call(pawn, L"GetHitPoint", "betrayal hp", 64);
            call.run();
            return *call.param(L"ReturnValue")->ContainerPtrToValuePtr<int>(call.parameters.data());
        };
        for (const auto& projectile : trap.projectiles)
        {
            if (auto* actor = projectile.actor.Get(); actor && projectile.owned)
            {
                result["projectiles"].push_back(
                    {{"actor", describe(actor)},
                     {"target",
                      describe(properties.read_object(properties.read_object(actor, L"MyMovement"),
                                                      L"m_HomingTargetActor"))}});
            }
        }
        if (controlled_player)
        {
            result["player_hp"] = hp(controlled_player);
            result["player"] = describe(controlled_player);
        }
        for (auto* pawn : slots)
        {
            if (!pawn)
            {
                continue;
            }
            auto* controller = properties.read_object(pawn, L"Controller");
            auto* effective_target = read_controller_target(controller);
            auto* stored_target =
                trap.running ? betrayal_target_hooks::stored_target(controller) : effective_target;
            auto* board_target =
                read_blackboard_target(properties.read_object(controller, L"Blackboard"));
            auto* collision = properties.read_object(pawn, L"MyAtkColl");
            auto weapons = nlohmann::json::array();
            for (auto* weapon : equipped_weapons(pawn))
            {
                weapons.push_back({{"weapon", describe(weapon)},
                                   {"team", *field(weapon, L"m_TeamNo", L"IntProperty", 4)
                                                 ->ContainerPtrToValuePtr<int>(weapon)},
                                   {"collision_team", *collision_team(properties.read_object(
                                                          weapon, L"MyAtkColl"))}});
            }
            result["companions"].push_back(
                {{"pawn", describe(pawn)},
                 {"weapons", weapons},
                 {"hp", hp(pawn)},
                 {"team",
                  *field(pawn, L"m_TeamNo", L"IntProperty", 4)->ContainerPtrToValuePtr<int>(pawn)},
                 {"ai_team", *field(pawn, L"MyTeam", L"ByteProperty", 1)
                                  ->ContainerPtrToValuePtr<uint8_t>(pawn)},
                 {"collision_team", *collision_team(collision)},
                 {"target", describe(stored_target)},
                 {"effective_target", describe(effective_target)},
                 {"blackboard_target", describe(board_target)},
                 {"position", party_native::actor_position(pawn).value_or(std::array<float, 3>{})},
                 {"can_battle",
                  party_native::invoke_named_bool_getter(pawn, L"CanBattle").value_or(false)}});
            auto* state = current_action(pawn);
            result["companions"].back()["action"] = describe(state);
            for (const auto* name : {L"m_Target", L"m_LockonTarget"})
            {
                auto* target_value = weak_target(state, name);
                result["companions"].back()[std::string(name, name + std::wcslen(name))] =
                    describe(target_value ? target_value->Get() : nullptr);
            }
        }
        return result;
    }
#endif
} // namespace kh3ap::betrayal_trap
