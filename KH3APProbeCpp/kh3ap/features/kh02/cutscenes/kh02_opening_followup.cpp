#include "kh02_opening_followup.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include <Unreal/Property/FBoolProperty.hpp>

#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {

        bool flag(UObject* object, const wchar_t* name)
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        float real(UObject* object, const wchar_t* name)
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || property->GetClass().GetName() != L"FloatProperty" ||
                property->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("kh02_story_float");
            }
            return *property->ContainerPtrToValuePtr<float>(object);
        }

    } // namespace

    void Kh02OpeningFollowup::bind(UObject* actor, UObject* source_script)
    {
        matinee = make_uobject_weak(actor);
        script = make_uobject_weak(source_script);
    }

    void Kh02OpeningFollowup::reset()
    {
        matinee = {};
        script = {};
        observation.reset();
    }

    bool Kh02OpeningFollowup::finished(const Kh02MovieBridge& movies)
    {
        if (!movies.error().empty())
        {
            throw std::runtime_error("kh02_opening_movie:" + movies.error());
        }
        auto* followup = matinee.Get();
        const bool valid = is_uobject_valid_fast(followup);
        const bool playing = valid && flag(followup, L"bIsPlaying");
        const float position = valid ? real(followup, L"InterpPosition") : 0.f;
        return observation.observe(valid, playing, position, is_uobject_valid_fast(script.Get()), movies.playing());
    }

} // namespace kh3ap
