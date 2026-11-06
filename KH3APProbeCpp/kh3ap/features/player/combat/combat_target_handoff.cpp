#include "kh3ap/features/player/combat/combat_target_handoff.h"
#include "kh3ap/features/player/combat/combat_blackboard_handoff.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <cstring>
#include <vector>
#include <algorithm>

namespace kh3ap::combat_target_handoff
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr const wchar_t* action_target_fields[]{L"m_Target", L"m_LockonTarget"};
        FWeakObjectPtr source, source_world;
        struct ActionTarget
        {
            FWeakObjectPtr state;
            const wchar_t* field;
        };
        std::vector<FWeakObjectPtr> controllers;
        std::vector<ActionTarget> actions;
        std::vector<FWeakObjectPtr> cached_players;
        ReflectedPropertyCache<64> properties;
        UObject* object(UObject* value, const wchar_t* field)
        {
            return properties.read_object(value, field);
        }
        UObject* target(UObject* controller)
        {
            Kh02ReflectedCall get(controller, L"GetMyTarget", "combat target handoff", 256);
            get.run();
            return try_read_function_object_param(get.function, get.parameters.data(), L"ReturnValue");
        }
        FWeakObjectPtr* action_target(UObject* state, const wchar_t* name)
        {
            static_assert(sizeof(FWeakObjectPtr) == 8);
            auto* property = properties.resolve(state, name);
            if (!property || property->GetClass().GetName() != L"WeakObjectProperty" ||
                property->GetElementSize() != sizeof(FWeakObjectPtr))
            {
                return nullptr;
            }
            return property->ContainerPtrToValuePtr<FWeakObjectPtr>(state);
        }
    }
    void cancel()
    {
        combat_blackboard_handoff::release();
        source = {};
        source_world = {};
        controllers.clear();
        actions.clear();
        cached_players.clear();
    }
    bool prepare(UObject* outgoing)
    {
        cancel();
        if (!is_uobject_valid_fast(outgoing)) return false;
        auto* enemy_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresEnemyPawnBase");
        auto* action_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresActionDefinitionBase");
        if (!is_uobject_valid_fast(enemy_class) || !is_uobject_valid_fast(action_class)) return false;
        source = make_uobject_weak(outgoing);
        source_world = make_uobject_weak(kh02_actor_world(outgoing));
        if (source.Get() != outgoing || !source_world.Get()) return false;
        combat_blackboard_handoff::capture_cameras(outgoing);
        const auto capture_action = [&](UObject* state)
        {
            if (!is_uobject_valid_fast(state) || !state->IsA(action_class)) return;
            for (const auto* name : action_target_fields)
            {
                const auto* cached = action_target(state, name);
                if (!cached || cached->Get() != outgoing) continue;
                if (std::none_of(actions.begin(), actions.end(), [&](const auto& ref) {
                    return ref.state.Get() == state && ref.field == name;
                }))
                {
                    actions.push_back({make_uobject_weak(state), name});
                }
            }
        };
        std::vector<AActor*> enemies;
        UGameplayStatics::GetAllActorsOfClass(outgoing, enemy_class, enemies);
        for (auto* enemy : enemies)
        {
            if (!is_uobject_valid_fast(enemy) || kh02_actor_world(enemy) != source_world.Get()) continue;
            // Titan scripts retain a strong player independently of AI targets.
            // Capture only exact references to the verified outgoing pawn.
            if (object(enemy, L"m_Player") == outgoing)
            {
                cached_players.push_back(make_uobject_weak(enemy));
            }
            combat_blackboard_handoff::capture(enemy, outgoing);
            auto* controller = object(enemy, L"Controller");
            if (is_uobject_valid_fast(controller) && controller->GetFunctionByNameInChain(L"GetMyTarget") &&
                controller->GetFunctionByNameInChain(L"SetMyTarget") && target(controller) == outgoing)
                controllers.push_back(make_uobject_weak(controller));
            auto* queue = object(enemy, L"MyStateComp");
            auto* current_state = object(queue, L"m_pCurrentState");
            capture_action(current_state);
            if (!is_uobject_valid_fast(queue)) continue;
            // Pending/recycled definitions can retain the same target until
            // their next execution. Only exact outgoing-pawn references move.
            for (const auto* name : {L"m_StateQueue", L"m_RecycleState"})
            {
                auto* property = queue->GetPropertyByNameInChain(name);
                if (!property || property->GetClass().GetName() != L"ArrayProperty" || property->GetElementSize() != 16) continue;
                auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(property)->GetInner());
                if (!inner || inner->GetClass().GetName() != L"ObjectProperty" || inner->GetElementSize() != sizeof(UObject*)) continue;
                auto* states = property->ContainerPtrToValuePtr<TArray<UObject*>>(queue);
                for (auto* state : *states) capture_action(state);
            }
        }
        combat_blackboard_handoff::suspend();
        return true;
    }
    void apply(UObject* incoming)
    {
        if (!source_world.Get()) return;
        if (source_world.Get() != kh02_current_world()) { cancel(); return; }
        auto* controller = object(incoming, L"Controller");
        if (!is_uobject_valid_fast(incoming) || incoming == source.Get() || object(controller, L"Pawn") != incoming ||
            kh02_actor_world(incoming) != source_world.Get()) return;
        const auto replacement = make_uobject_weak(incoming);
        if (replacement.Get() != incoming) return;
        // Blackboard observers may abort/update actions synchronously. Repair
        // their cached weak targets after committing the authoritative keys.
        combat_blackboard_handoff::apply(incoming);
        size_t action_count = 0;
        size_t controller_count = 0;
        size_t already_targeting = 0;
        size_t cleared_restored = 0;
        size_t cached_player_count = 0;
        for (const auto& reference : cached_players)
        {
            auto* enemy = reference.Get();
            if (!enemy || kh02_actor_world(enemy) != source_world.Get()) continue;
            auto* property = properties.resolve_object(enemy, L"m_Player");
            if (!property) continue;
            auto** cached = property->ContainerPtrToValuePtr<UObject*>(enemy);
            if (*cached && *cached != source.Get()) continue;
            *cached = incoming;
            ++cached_player_count;
        }
        // Repair active definitions first: the native AI setter may notify
        // its blackboard observers synchronously.
        for (const auto& ref : actions)
        {
            auto* state = ref.state.Get();
            if (!state) continue;
            auto* cached = action_target(state, ref.field);
            if (!cached) continue;
            // ChangePlayer can clear a running action's captured target while
            // retiring the pawn. Native action ticks can still dereference it.
            // Only fields verified to target the outgoing pawn are eligible.
            if (std::memcmp(cached, &source, sizeof(source)) != 0)
            {
                if (cached->Get()) continue;
                ++cleared_restored;
            }
            *cached = replacement;
            ++action_count;
        }
        for (const auto& ref : controllers)
        {
            auto* ai = ref.Get();
            if (!ai || kh02_actor_world(ai) != source_world.Get()) continue;
            auto* current = target(ai);
            if (current == incoming)
            {
                ++already_targeting;
                continue;
            }
            if (current && current != source.Get()) continue;
            Kh02ReflectedCall set(ai, L"SetMyTarget", "combat target handoff", 256);
            set.object(L"inNewMyTarget", incoming);
            set.run();
            if (target(ai) == incoming) ++controller_count;
        }
        if (action_count || controller_count || cleared_restored || cached_player_count)
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Combat target handoff: controllers={} already_targeting={} actions={} cleared_restored={} cached_players={} incoming={}\n"),
            controller_count, already_targeting, action_count, cleared_restored, cached_player_count, incoming->GetName());
    }
    void finish(UObject* incoming)
    {
        apply(incoming);
        cancel();
    }
}

