#include "kh02_story_targets.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr std::size_t event_history_limit = 64;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        auto flag(UObject* object, const wchar_t* name) -> bool
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

        constexpr std::size_t story_target_limit = 512;
        constexpr int target_component_limit = 32;
    } // namespace

    void Kh02StoryTargets::watch(UObject* actor)
    {
        if (!is_uobject_valid_fast(actor) || !actor->GetPathName().starts_with(L"/Game/Zevels/dw/") ||
            !actor->GetClassPrivate()->GetPathName().starts_with(L"/Game/Zlueprints/Gimmick/"))
        {
            return;
        }
        auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresGimmickActor");
        if (!is_uobject_a_fast(actor, cls))
        {
            return;
        }
        for (const auto& item : story_targets)
        {
            if (item.actor.Get() == actor)
            {
                return;
            }
        }
        if (story_targets.size() >= story_target_limit)
        {
            target_error = "kh02_story_target_limit";
            return;
        }
        story_targets.push_back({make_uobject_weak(actor)});
        story_targets_pending = true;
    }

    void Kh02StoryTargets::request_sync(UObject* changed)
    {
        bool requested = false;
        for (auto& item : story_targets)
        {
            if (!changed || item.actor.Get() == changed)
            {
                item.dirty = true;
                story_targets_pending = true;
                requested = true;
            }
        }
        if (requested)
        {
            ++target_notifications;
        }
    }

    void Kh02StoryTargets::sync(Actor& item)
    {
        auto* actor = item.actor.Get();
        if (!is_uobject_valid_fast(actor))
        {
            return;
        }
        if (!item.discovered)
        {
            auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                                                  L"/Script/TresGame.TresLockonTargetComponent");
            if (!is_uobject_valid_fast(cls))
            {
                throw std::runtime_error("kh02_story_target_class");
            }
            auto components = static_cast<AActor*>(actor)->K2_GetComponentsByClass(cls);
            if (components.Num() > target_component_limit)
            {
                throw std::runtime_error("kh02_story_target_component_limit");
            }
            for (auto* component : components)
            {
                if (is_uobject_valid_fast(component) && component->GetOuterPrivate() == actor)
                {
                    item.targets.push_back({make_uobject_weak(component)});
                }
            }
            item.discovered = true;
        }
        // Actor hiding does not remove KH3's independently registered lock-on
        // points. Only restore components that this visibility guard disabled;
        // a visible prop disabled by its own script must remain disabled.
        const bool hidden = flag(actor, L"bHidden");
        for (auto& target : item.targets)
        {
            if (auto* component = target.component.Get(); is_uobject_valid_fast(component))
            {
                const bool active = flag(component, L"m_bIsGimmickActive");
                if ((hidden && active) || (!hidden && target.suppressed))
                {
                    ReflectedCall change(component,
                                         hidden ? L"BP_DeactivateGimmickComponent" : L"BP_ActivateGimmickComponent");
                    change.run();
                    if (flag(component, L"m_bIsGimmickActive") == hidden)
                    {
                        throw std::runtime_error("kh02_story_target_activation_readback");
                    }
                    target.suppressed = hidden;
                    if (target_events.size() < event_history_limit)
                    {
                        target_events.push_back({{"actor", path(actor)},
                                                 {"component", path(component)},
                                                 {"hidden", hidden},
                                                 {"active_before", active},
                                                 {"active_after", !hidden}});
                    }
                }
            }
        }
    }

    void Kh02StoryTargets::tick(UObject* world)
    {
        if (!story_targets_pending && target_world.Get() == world)
        {
            return;
        }
        target_world = make_uobject_weak(world);
        story_targets_pending = false;
        std::erase_if(story_targets,
                      [world](const auto& item)
                      {
                          auto* actor = item.actor.Get();
                          return !is_uobject_valid_fast(actor) || kh02_actor_world(actor) != world;
                      });
        for (auto& item : story_targets)
        {
            if (!item.dirty)
            {
                continue;
            }
            item.dirty = false;
            try
            {
                sync(item);
                ++target_syncs;
            }
            catch (const std::exception& e)
            {
                target_error = e.what();
            }
        }
    }

    auto Kh02StoryTargets::snapshot() const -> Json
    {
        Json out = {{"actors", Json::array()}, {"events", target_events},
                    {"error", target_error},   {"notifications", target_notifications},
                    {"syncs", target_syncs},   {"pending", story_targets_pending}};
        for (const auto& item : story_targets)
        {
            if (auto* actor = item.actor.Get(); is_uobject_valid_fast(actor))
            {
                Json row = {{"actor", path(actor)}, {"hidden", flag(actor, L"bHidden")}, {"components", Json::array()}};
                for (const auto& target : item.targets)
                {
                    if (auto* component = target.component.Get(); is_uobject_valid_fast(component))
                    {
                        row["components"].push_back({{"component", path(component)},
                                                     {"active", flag(component, L"m_bIsGimmickActive")},
                                                     {"permission_lockon", flag(component, L"m_bPermissionLockOn")},
                                                     {"suppressed", target.suppressed}});
                    }
                }
                if (!row["components"].empty())
                {
                    out["actors"].push_back(std::move(row));
                }
            }
        }
        return out;
    }

    void Kh02StoryTargets::clear_world()
    {
        story_targets.clear();
        target_world = {};
        story_targets_pending = false;
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02StoryTargets::visibility_fixture(UObject* world, const std::function<void()>& observe_world) -> Json
    {
        for (auto& item : story_targets)
        {
            if (auto* actor = item.actor.Get(); is_uobject_valid_fast(actor) && kh02_actor_world(actor) == world &&
                                                actor->GetClassPrivate()->GetName() == L"g_dw_Gear_C" &&
                                                flag(actor, L"bHidden") && !item.targets.empty())
            {
                Json proof = {{"actor", path(actor)}, {"before", snapshot()}};
                // Keep the fixture reversible even if a reflected read fails.
                try
                {
                    ReflectedCall show(actor, L"SetActorHiddenInGame");
                    show.boolean(L"bNewHidden", false);
                    show.run();
                    observe_world();
                    tick(world);
                    proof["shown"] = snapshot();
                    auto* component = item.targets.front().component.Get();
                    ReflectedCall disable(component, L"BP_DeactivateGimmickComponent");
                    disable.run();
                    observe_world();
                    tick(world);
                    proof["script_disabled_visible"] = snapshot();
                    ReflectedCall enable(component, L"BP_ActivateGimmickComponent");
                    enable.run();
                }
                catch (...)
                {
                    ReflectedCall hide(actor, L"SetActorHiddenInGame");
                    hide.boolean(L"bNewHidden", true);
                    hide.run();
                    throw;
                }
                ReflectedCall hide(actor, L"SetActorHiddenInGame");
                hide.boolean(L"bNewHidden", true);
                hide.run();
                observe_world();
                tick(world);
                proof["restored"] = snapshot();
                auto* component = item.targets.front().component.Get();
                ReflectedCall activate(component, L"BP_ActivateGimmickComponent");
                activate.run();
                observe_world();
                tick(world);
                proof["hidden_reactivated"] = snapshot();
                ReflectedCall plan_show(actor, L"RequestShowGimmickForPlan");
                plan_show.run();
                observe_world();
                tick(world);
                proof["plan_shown"] = snapshot();
                ReflectedCall plan_hide(actor, L"RequestHideGimmickForPlan");
                plan_hide.run();
                observe_world();
                tick(world);
                proof["plan_restored"] = snapshot();
                return proof;
            }
        }
        throw std::runtime_error("kh02_hidden_gear_fixture_missing");
    }
#endif
} // namespace kh3ap
