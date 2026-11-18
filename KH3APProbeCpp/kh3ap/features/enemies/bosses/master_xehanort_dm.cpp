#include "master_xehanort_dm.h"
#include "master_xehanort_center_policy.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        struct Cleanup
        {
            FWeakObjectPtr effect;
            FWeakObjectPtr boss;
            const wchar_t* reason{};
        };

        std::mutex effects_mutex;
        std::vector<FWeakObjectPtr> effects;
        std::vector<Cleanup> pending;
        std::atomic<bool> have_effects{};
        std::atomic<bool> have_pending{};
        std::atomic<bool> installed{};
        FName ascension_name;
        FName boss_name;
        FName killed_event;
        FName end_play_event;
        FName reset_event;

        auto exact_class(UObject* object, FName name, std::wstring_view path) -> bool
        {
            auto* type = is_uobject_valid_fast(object) ? object->GetClassPrivate() : nullptr;
            return is_uobject_valid_fast(type) && type->GetNamePrivate() == name &&
                   type->GetPathName() == path;
        }

        auto is_phase_reset(std::wstring_view name) -> bool
        {
            constexpr std::wstring_view marker = L"_REvt_BattleArea_";
            const auto offset = name.find(marker);
            if (offset == name.npos)
            {
                return false;
            }
            const auto suffix = name.substr(offset + marker.size());
            constexpr std::wstring_view set_phase = L"_SetPhase";
            const auto phase = suffix.find(set_phase);
            return phase != suffix.npos && phase > 0 && phase + set_phase.size() < suffix.size();
        }

        void queue_cleanup(UObject* world, UObject* boss, const wchar_t* reason)
        {
            if (!is_uobject_valid_fast(world))
            {
                return;
            }
            std::lock_guard lock{effects_mutex};
            std::erase_if(effects, [&](const FWeakObjectPtr& effect)
            {
                auto* actor = effect.Get();
                if (!is_uobject_valid_fast(actor))
                {
                    return true;
                }
                if (!exact_class(actor, ascension_name, data_master_ascension_class) ||
                    actor_owning_world(actor) != world)
                {
                    return false;
                }
                auto* parent = try_read_reflected_object_property_value(actor, L"m_MyParent");
                if (boss && parent && parent != boss)
                {
                    return false;
                }
                pending.push_back({effect, make_uobject_weak(parent ? parent : boss), reason});
                return true;
            });
            have_effects.store(!effects.empty(), std::memory_order_release);
            have_pending.store(!pending.empty(), std::memory_order_release);
        }

        void on_battle_event(UObject* context, UFunction* function, void* params)
        {
            if (!have_effects.load(std::memory_order_acquire) || !function)
            {
                return;
            }
            const auto name = function->GetNamePrivate();
            if (name == killed_event || name == end_play_event || name == reset_event)
            {
                auto* pawn = name == killed_event ?
                    try_read_function_object_param(function, params, L"Pawn") : context;
                if (exact_class(pawn, boss_name, data_master_xehanort_class))
                {
                    queue_cleanup(actor_owning_world(pawn), pawn,
                                  name == killed_event ? L"defeat" : L"boss_reset");
                }
                return;
            }

            // Only authored phase events on the streaming level's own script.
            // An ordinary ally death can leave the current arena running.
            if (!is_uobject_valid_fast(context) || !is_phase_reset(function->GetName()))
            {
                return;
            }
            auto* level = context->GetOuterPrivate();
            if (try_read_reflected_object_property_value(level, L"LevelScriptActor") == context)
            {
                queue_cleanup(actor_owning_world(context), nullptr, L"arena_phase");
            }
        }

        void retire(const Cleanup& cleanup)
        {
            auto* actor = cleanup.effect.Get();
            if (!exact_class(actor, ascension_name, data_master_ascension_class))
            {
                return;
            }
            auto* boss = cleanup.boss.Get();
            // Sever only the link to this effect. A fresh Ascension spawned
            // after the transition already belongs to the next phase.
            if (exact_class(boss, boss_name, data_master_xehanort_class))
            {
                auto* property = boss->GetPropertyByNameInChain(L"m_AscensionObject");
                if (property && property->GetClass().GetName() == L"ObjectProperty" &&
                    property->GetElementSize() == sizeof(UObject*))
                {
                    auto** link = property->ContainerPtrToValuePtr<UObject*>(boss);
                    if (link && *link == actor)
                    {
                        *link = nullptr;
                    }
                }
            }
            const auto path = actor->GetPathName();
            // Destruction takes the native helper's teardown path and
            // unregisters its particle and projected decal components.
            static_cast<AActor*>(actor)->K2_DestroyActor();
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Master Xehanort Ascension retired reason={} actor={}\n"),
                cleanup.reason, path);
        }
    }

    void install_master_xehanort_dm_cleanup()
    {
        if (installed.load(std::memory_order_acquire))
        {
            return;
        }
        ascension_name = FName{L"e_ex367_AscensionPawn_C", FNAME_Add};
        boss_name = FName{L"e_ex367_Pawn_C", FNAME_Add};
        killed_event = FName{L"ReceiveTresEnemyKilled", FNAME_Add};
        end_play_event = FName{L"ReceiveEndPlay", FNAME_Add};
        reset_event = FName{L"K2_OnReset", FNAME_Add};
        Hook::RegisterProcessEventPreCallback(&on_battle_event);
        installed.store(true, std::memory_order_release);
    }

    void observe_master_xehanort_dm_actor(UObject* actor)
    {
        if (!installed.load(std::memory_order_acquire) || !actor || !actor->GetClassPrivate() ||
            actor->GetClassPrivate()->GetNamePrivate() != ascension_name ||
            actor->HasAnyFlags(static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)))
        {
            return;
        }
        const auto weak = make_uobject_weak(actor);
        if (weak.Get() == actor)
        {
            std::lock_guard lock{effects_mutex};
            std::erase_if(effects, [](const FWeakObjectPtr& effect) { return !effect.Get(); });
            effects.push_back(weak);
            have_effects.store(true, std::memory_order_release);
        }
    }

    void notify_master_xehanort_battle_ended(UObject* player)
    {
        if (have_effects.load(std::memory_order_acquire))
        {
            queue_cleanup(actor_owning_world(player), nullptr, L"battle_end");
        }
    }

    void process_pending_master_xehanort_dm_cleanup()
    {
        if (!have_pending.load(std::memory_order_acquire))
        {
            return;
        }
        std::vector<Cleanup> work;
        {
            std::lock_guard lock{effects_mutex};
            work.swap(pending);
            have_pending.store(false, std::memory_order_release);
        }
        // Leave native death and Blueprint phase dispatch before cleanup.
        for (const auto& cleanup : work)
        {
            retire(cleanup);
        }
    }
}
