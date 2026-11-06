#pragma once
#include "kh02_challenge_policy.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <optional>
#include <string>

namespace kh3ap
{
    // Called only after the tracker accepts its current thread/component/world.
    // Source state and unfinished rail resolution retain weak identities across frames.
    class Kh02ChallengePlayerStateObserver
    {
      public:
        auto accept(RC::Unreal::UObject* state) -> std::optional<std::wstring>;
        void resolve_pending_rail();
        auto sample(RC::Unreal::UObject* pawn) -> int;
        void reset(int saved_slide_meters);

        auto event_count() const -> uint64_t
        {
            return state_events;
        }

        auto state_name() const -> const std::string&
        {
            return last_state;
        }

        auto rail_name() const -> const std::string&
        {
            return last_rail_actor;
        }

        auto sliding() const -> bool
        {
            return ice_slide.active;
        }

        auto slide_centimeters() const -> double
        {
            return ice_slide.centimeters;
        }

      private:
        kh02_tracking::IceSlide ice_slide;
        RC::Unreal::FWeakObjectPtr last_player_state, rail_state;
        uint64_t state_events{};
        std::string last_state, last_rail_actor;
    };
} // namespace kh3ap
