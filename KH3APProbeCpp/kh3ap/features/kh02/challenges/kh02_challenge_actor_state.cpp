#include "kh02_challenge_actor_state.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t rail_query_parameter_limit_bytes = 32;
    }

    UObject* kh02_challenge_current_state(UObject* actor)
    {
        if (!is_uobject_valid_fast(actor))
        {
            return nullptr;
        }
        auto* component = try_read_reflected_object_property_value(actor, L"MyStateComp");
        return is_uobject_valid_fast(component)
                   ? try_read_reflected_object_property_value(component, L"m_pCurrentState")
                   : nullptr;
    }

    bool kh02_is_blizzard_rail(UObject* rail)
    {
        if (!is_uobject_valid_fast(rail))
        {
            return false;
        }
        // TresProj_MagicBlizzard adds this tag to each generated rail.
        // Ordinary forest rails use the same native actor class.
        Kh02ReflectedCall query(rail, L"ActorHasTag", "kh02_challenge", rail_query_parameter_limit_bytes);
        query.name(L"Tag", L"BlizzardRail");
        query.run();
        return query.boolean_result();
    }

} // namespace kh3ap
