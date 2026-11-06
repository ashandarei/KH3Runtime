#include "kh02_movie_actor_visibility.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <stdexcept>
#include <algorithm>
#include "kh3ap/core/objects/actor_discovery.h"

namespace
{
    constexpr int maximum_scene_actor_candidates = 16'384;
}

namespace kh3ap
{
    using namespace RC::Unreal;

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

    } // namespace

    void Kh02MovieActorVisibility::collect(UObject* matinee, UObject* pawn)
    {
        // Keep ownership until the map session retires. The previous
        // scene can remain streamed while the next movie is prepared.
        retired_movie_hidden_actors.insert(retired_movie_hidden_actors.end(), ending_movie_hidden_actors.begin(),
                                           ending_movie_hidden_actors.end());
        ending_movie_hidden_actors.clear();
        auto* actor_class = static_cast<UClass*>(kh02_require_loaded_object(L"/Script/Engine.Actor"));
        const auto scene_prefix = matinee->GetOuterPrivate()->GetPathName() + L".";
        for (auto* actor : find_live_actors_of_class(pawn, actor_class, maximum_scene_actor_candidates))
        {
            if (actor->GetPathName().starts_with(scene_prefix))
            {
                ending_movie_hidden_actors.push_back({make_uobject_weak(actor), flag(actor, L"bHidden")});
            }
        }
    }

    void Kh02MovieActorVisibility::tick(UObject* world)
    {
        std::erase_if(ending_movie_hidden_actors,
                      [&](const auto& entry)
                      {
                          auto* actor = entry.actor.Get();
                          if (!is_uobject_valid_fast(actor) || kh02_actor_world(actor) != world)
                          {
                              return true;
                          }
                          if (!flag(actor, L"bHidden"))
                          {
                              ReflectedCall hide(actor, L"SetActorHiddenInGame");
                              hide.boolean(L"bNewHidden", true);
                              hide.run();
                          }
                          return false;
                      });
    }

    void Kh02MovieActorVisibility::cancel() noexcept
    {
        const auto restore_actor = [&](const HiddenActor& entry)
        {
            try
            {
                if (auto* actor = entry.actor.Get(); is_uobject_valid_fast(actor))
                {
                    ReflectedCall release(actor, L"SetActorHiddenInGame");
                    release.boolean(L"bNewHidden", entry.originally_hidden);
                    release.run();
                }
            }
            catch (...)
            {
            }
        };
        for (const auto& actor : ending_movie_hidden_actors)
        {
            restore_actor(actor);
        }
        for (auto actor = retired_movie_hidden_actors.rbegin(); actor != retired_movie_hidden_actors.rend(); ++actor)
        {
            restore_actor(*actor);
        }

        ending_movie_hidden_actors.clear();
        retired_movie_hidden_actors.clear();
    }

} // namespace kh3ap
