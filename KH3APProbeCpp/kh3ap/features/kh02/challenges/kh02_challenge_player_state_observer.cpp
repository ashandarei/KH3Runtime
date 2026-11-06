#include "kh02_challenge_player_state_observer.h"
#include "kh02_challenge_actor_state.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <algorithm>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto Kh02ChallengePlayerStateObserver::accept(UObject* state) -> std::optional<std::wstring>
    {
        if (last_player_state.Get() == state)
        {
            return std::nullopt;
        }
        last_player_state = make_uobject_weak(state);
        auto name = is_uobject_valid_fast(state) ? state->GetClassPrivate()->GetName() : L"";
        ++state_events;
        last_state = to_utf8(name);
        if (name != L"TresPlayerState_RailSlide")
        {
            ice_slide.end();
            rail_state = {};
        }
        if (name == L"TresPlayerState_RailSlide")
        {
            auto* rail = try_read_reflected_object_property_value(state, L"m_SplineActor");
            const auto rail_name = is_uobject_valid_fast(rail) ? rail->GetClassPrivate()->GetName() : L"";
            last_rail_actor = to_utf8(rail_name);
            ice_slide.end();
            rail_state = make_uobject_weak(state);
            ice_slide.active = kh02_is_blizzard_rail(rail);
        }
        return name;
    }

    void Kh02ChallengePlayerStateObserver::resolve_pending_rail()
    {
        if (!ice_slide.active && is_uobject_valid_fast(rail_state.Get()))
        {
            // The state writer runs before the rail constructor fills this
            // pointer. Resolve it only while that rail state is current.
            auto* rail = try_read_reflected_object_property_value(rail_state.Get(), L"m_SplineActor");
            if (is_uobject_valid_fast(rail))
            {
                const auto name = rail->GetClassPrivate()->GetName();
                last_rail_actor = to_utf8(name);
                ice_slide.active = kh02_is_blizzard_rail(rail);
                rail_state = {};
            }
        }
    }

    auto Kh02ChallengePlayerStateObserver::sample(UObject* pawn) -> int
    {
        constexpr size_t location_parameter_limit_bytes = 64;
        Kh02ReflectedCall location(pawn, L"K2_GetActorLocation", "kh02_challenge", location_parameter_limit_bytes);
        location.run();
        return ice_slide.sample(location.result_vector());
    }

    void Kh02ChallengePlayerStateObserver::reset(int saved_slide_meters)
    {
        ice_slide.reset();
        ice_slide.centimeters =
            std::min(kh02_tracking::ice_slide_goal_meters, saved_slide_meters) * kh02_tracking::centimeters_per_meter;
        last_player_state = {};
        rail_state = {};
    }
} // namespace kh3ap
