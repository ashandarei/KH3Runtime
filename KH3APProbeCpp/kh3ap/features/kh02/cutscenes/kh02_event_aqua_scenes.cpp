#include "kh02_event_aqua_scenes.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_cutscene", reflected_parameter_limit_bytes)
            {
            }
        };

        bool flag(UObject* object, const wchar_t* property_name)
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(property_name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        std::string path(UObject* object)
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

    } // namespace

    void Kh02EventAquaScenes::track(UObject* matinee_actor, UObject* event_pawn)
    {
        if (event_aqua_owner.Get() != event_pawn)
        {
            event_aqua_scenes.clear();
            event_aqua_owner = make_uobject_weak(event_pawn);
            event_aqua_scene_observed = false;
        }
        for (const auto& scene : event_aqua_scenes)
        {
            if (scene.matinee.Get() == matinee_actor)
            {
                return;
            }
        }
        event_aqua_scenes.push_back({make_uobject_weak(matinee_actor), false});
    }

    void Kh02EventAquaScenes::tick()
    {
        auto* event_pawn = event_aqua_owner.Get();
        if (!is_uobject_valid_fast(event_pawn))
        {
            event_aqua_scenes.clear();
            event_aqua_scene_observed = false;
            return;
        }
        bool playing = false;
        for (auto iterator = event_aqua_scenes.begin(); iterator != event_aqua_scenes.end();)
        {
            auto* scene = iterator->matinee.Get();
            if (!is_uobject_valid_fast(scene) || kh02_actor_world(scene) != kh02_actor_world(event_pawn))
            {
                iterator = event_aqua_scenes.erase(iterator);
                continue;
            }
            if (flag(scene, L"bIsPlaying"))
            {
                iterator->started = true;
                playing = true;
                ++iterator;
            }
            else if (iterator->started)
            {
                iterator = event_aqua_scenes.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        event_aqua_scene_observed |= playing;
        if (event_aqua_scene_observed && !playing)
        {
            // The event pawn belongs to our host, not the streamed cutscene.
            // Retire its presentation after the real Matinee stops (including
            // Skip or unload), keeping the reusable actor for the next scene.
            ReflectedCall hide(event_pawn, L"SetActorHiddenInGame");
            hide.boolean(L"bNewHidden", true);
            hide.run();
            event_aqua_scene_observed = false;
            ++event_aqua_cleanups;
        }
    }

    Json Kh02EventAquaScenes::snapshot() const
    {
        auto* event_pawn = event_aqua_owner.Get();
        return {{"actor", path(event_pawn)},
                {"hidden", is_uobject_valid_fast(event_pawn) ? Json(flag(event_pawn, L"bHidden")) : Json{}},
                {"tracked", event_aqua_scenes.size()},
                {"playing", event_aqua_scene_observed},
                {"cleanups", event_aqua_cleanups}};
    }

    void Kh02EventAquaScenes::cancel() noexcept
    {
        try
        {
            if (auto* event_pawn = event_aqua_owner.Get(); is_uobject_valid_fast(event_pawn))
            {
                ReflectedCall hide(event_pawn, L"SetActorHiddenInGame");
                hide.boolean(L"bNewHidden", true);
                hide.run();
            }
        }
        catch (...)
        {
        }
        event_aqua_scenes.clear();
        event_aqua_owner = {};
        event_aqua_scene_observed = false;
    }

} // namespace kh3ap
