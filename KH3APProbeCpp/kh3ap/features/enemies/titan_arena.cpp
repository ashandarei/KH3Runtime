#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/features/enemies/titan_object_registry.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <array>
#include <chrono>
#include <vector>
#include <stdexcept>
#include <mutex>

namespace kh3ap
{
    namespace
    {
        constexpr auto titan_obstacle_readiness_retry = std::chrono::milliseconds(100);
        constexpr auto titan_obstacle_failure_retry = std::chrono::seconds(10);
    }

    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        std::mutex entity_mutex;
        std::array<std::wstring,2> targets;
        std::atomic<uint64_t> binding_revision{};
        uint64_t seen_binding{~uint64_t{0}}, seen_objects{~uint64_t{0}};
        RuntimeEventWork work;
        bool was_enabled{};
        struct Original { FWeakObjectPtr actor; bool collision{}, hidden{}; };
        std::vector<Original> originals;
        constexpr std::wstring_view entities[]{
            L"/Game/Levels/he/he_05/umap/he_05_enemy_01.he_05_enemy_01:PersistentLevel.e_he902_Pawn1317",
            L"/Game/Levels/he/he_05/umap/he_05_enemy_01.he_05_enemy_01:PersistentLevel.e_he904_Pawn1045",
        };
        constexpr std::wstring_view sources[]{
            L"/Game/Blueprints/Enemy/e_he902/e_he902_Pawn.e_he902_Pawn_C",
            L"/Game/Blueprints/Enemy/e_he904/e_he904_Pawn.e_he904_Pawn_C",
        };
        bool bool_call(UObject* object, const wchar_t* name, const wchar_t* param, bool value)
        {
            auto* fn = object->GetFunctionByNameInChain(name);
            auto* p = fn ? find_function_param_by_name(fn, param) : nullptr;
            if (!p || fn->GetParmsSize() != 1 || p->GetOffset_Internal() != 0 ||
                    p->GetClass().GetName() != L"BoolProperty") throw std::runtime_error("titan_arena_bool_layout");
            uint8_t buffer = value;
            object->ProcessEvent(fn, &buffer);
            return param == std::wstring_view(L"ReturnValue") ? buffer != 0 : true;
        }
        bool hidden_value(UObject* actor)
        {
            auto* field = actor->GetPropertyByNameInChain(L"bHidden");
            if (!field || field->GetClass().GetName() != L"BoolProperty")
                throw std::runtime_error("titan_arena_hidden_layout");
            return static_cast<FBoolProperty*>(field)->GetPropertyValueInContainer(actor);
        }
        void restore()
        {
            for (const auto& original : originals)
            {
                auto* actor = original.actor.Get();
                if (!is_uobject_valid_fast(actor)) continue;
                bool_call(actor, L"SetActorEnableCollision", L"bNewActorEnableCollision", original.collision);
                bool_call(actor, L"SetActorHiddenInGame", L"bNewHidden", original.hidden);
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Titan arena obstacle restored actor={} collision_enabled={} hidden={}\n"),
                    actor->GetPathName(), bool_call(actor,L"GetActorEnableCollision",L"ReturnValue",true), hidden_value(actor));
            }
            originals.clear();
        }
    }

    void configure_titan_arena_replacement(std::wstring_view source, std::wstring_view target)
    {
        for (size_t i=0;i<2;++i)
            if (source==sources[i]) titan_objects::watch_pawn_class(i,target);
    }

    void track_titan_arena_entity(UObject* entity, std::wstring_view source, std::wstring_view target)
    {
        if (!is_uobject_valid_fast(entity)) return;
        const auto path=entity->GetPathName();
        for (size_t i=0; i<2; ++i)
        {
            if (path!=entities[i] || source!=sources[i]) continue;
            std::lock_guard lock{entity_mutex};
            targets[i]=target;
            binding_revision.fetch_add(1,std::memory_order_release);
        }
    }

    void notify_titan_arena_binding(std::wstring_view path)
    {
        for (const auto entity : entities)
            if (path==entity) { binding_revision.fetch_add(1,std::memory_order_release); return; }
    }

    void process_titan_arena_replacement(bool enabled)
    {
        titan_objects::initialize();
        const auto objects=titan_objects::generation();
        const auto binding=binding_revision.load(std::memory_order_acquire);
        if (objects!=seen_objects || binding!=seen_binding || enabled!=was_enabled) work.request();
        seen_objects=objects;seen_binding=binding;was_enabled=enabled;
        const auto now=Clock::now();
        if (!work.take(now)) return;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeHotPathProfileScope profile{L"enemies:titan_obstacles"};
#endif
        try
        {
            if (!enabled) { if (!originals.empty()) restore(); return; }
            const auto snapshot=titan_objects::snapshot();

            std::array<UObject*,2> tracked_entities{}, pawns{};
            std::array<UObject*,3> obstacles{};
            for (auto* actor : snapshot.objects)
            {
                const auto path=actor->GetPathName();
                for (size_t i=0;i<tracked_entities.size();++i)
                    if (path==entities[i]) tracked_entities[i]=actor;
                for (size_t i=0;i<obstacles.size();++i)
                    if (path==titan_arena_obstacles[i]) obstacles[i]=actor;
            }
            bool unfinished=snapshot.unfinished;
            for (auto* candidate : snapshot.pawns)
            {
                bool same_level=false, bound=false;
                for (auto* entity : tracked_entities)
                    if (entity && entity->GetOuterPrivate()==candidate->GetOuterPrivate())
                    {
                        same_level=true;
                        bound |= try_read_reflected_object_property_value(entity,L"m_EntityActor")==candidate;
                    }
                // Constructor notification precedes FinishSpawning's owner write.
                // Retry only an actually constructed, still-unbound pawn. Merely
                // loading an inactive arena never starts a spawn readiness poll.
                unfinished |= same_level && !bound;
            }
            work.finish(now,!unfinished,titan_obstacle_readiness_retry);
            {
                std::lock_guard lock{entity_mutex};
                for (size_t i=0;i<tracked_entities.size();++i)
                {
                    auto* entity=tracked_entities[i];
                    if (!entity || targets[i].empty()) continue;
                    auto* pawn=try_read_reflected_object_property_value(entity,L"m_EntityActor");
                    if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(pawn->GetClassPrivate())) continue;
                    const auto actual=pawn->GetClassPrivate()->GetPathName();
                    if (actual!=targets[i] || actual.starts_with(L"/Game/Blueprints/Enemy/e_he90"))
                    { if (!originals.empty()) restore(); return; }
                    pawns[i]=pawn;
                }
            }
            // Keep removal during Retry's no-pawn interval. The ownership-bind
            // event wakes us when its replacement pawns have been assigned.
            if (!pawns[0] || !pawns[1] || pawns[0]->GetOuterPrivate()!=pawns[1]->GetOuterPrivate()) return;
            // Wait for all three streamed actors before changing any geometry.
            for (auto* actor : obstacles) if (!actor) return;
            // Retry may replace the level actors. Never use the previous
            // encounter's saved values as the new actors' restoration record.
            if (!originals.empty())
            {
                bool same = originals.size() == obstacles.size();
                for (size_t i = 0; same && i < obstacles.size(); ++i)
                    same = originals[i].actor.Get() == obstacles[i];
                if (!same) restore();
            }
            if (originals.empty())
            {
                std::vector<Original> captured;
                for (auto* actor : obstacles)
                {
                    Original original;
                    original.actor=make_uobject_weak(actor);
                    if (original.actor.Get()!=actor) throw std::runtime_error("titan_arena_weak_unavailable");
                    original.collision = bool_call(actor,L"GetActorEnableCollision",L"ReturnValue",true);
                    original.hidden = hidden_value(actor);
                    captured.push_back(original);
                }
                originals=std::move(captured);
            }
            bool complete = true;
            for (size_t i = 0; i < obstacles.size(); ++i)
            {
                auto* actor = obstacles[i];
                const bool before=bool_call(actor,L"GetActorEnableCollision",L"ReturnValue",true);
                const bool was_hidden=hidden_value(actor);
                const bool changed=!before || bool_call(actor,L"SetActorEnableCollision",L"bNewActorEnableCollision",false);
                bool hidden = true;
                if (i == 2)
                {
                    hidden = was_hidden || bool_call(actor, L"SetActorHiddenInGame", L"bNewHidden", true);
                    hidden = hidden && hidden_value(actor);
                }
                const bool collision = bool_call(actor, L"GetActorEnableCollision", L"ReturnValue", true);
                complete &= changed && !collision && hidden;
                if (before || (i==2 && !was_hidden))
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Titan arena obstacle actor={} collision_enabled={} hidden_verified={} both_titans_replaced=true\n"),
                        actor->GetPathName(), collision, hidden);
            }
            if (!complete) throw std::runtime_error("titan_arena_readback");
        }
        catch (...) { work.finish(now,false,titan_obstacle_failure_retry); Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Titan arena obstacle update failed\n")); }
    }
}
