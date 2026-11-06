#include "kh02_scene_hero_visibility.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
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

    void Kh02SceneHeroVisibility::tick(UObject* pawn, bool playing)
    {
        if (playing)
        {
            if (hidden_hero.Get() != pawn)
            {
                hidden_hero = make_uobject_weak(pawn);
                hero_was_hidden = flag(pawn, L"bHidden");
            }
            if (!flag(pawn, L"bHidden"))
            {
                ReflectedCall hide(pawn, L"SetActorHiddenInGame");
                hide.boolean(L"bNewHidden", true);
                hide.run();
            }
        }
        else if (auto* hidden = hidden_hero.Get(); is_uobject_valid_fast(hidden))
        {
            ReflectedCall restore(hidden, L"SetActorHiddenInGame");
            restore.boolean(L"bNewHidden", hero_was_hidden);
            restore.run();
            hidden_hero = {};
        }
    }

} // namespace kh3ap
