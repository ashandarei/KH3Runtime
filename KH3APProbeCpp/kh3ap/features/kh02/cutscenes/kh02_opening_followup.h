#pragma once
#include "kh02_opening_followup_state.h"
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    class Kh02MovieBridge;

    class Kh02OpeningFollowup
    {
      public:
        void bind(RC::Unreal::UObject* actor, RC::Unreal::UObject* script);
        void reset();
        auto finished(const Kh02MovieBridge& movies) -> bool;

        auto source_script() const -> RC::Unreal::UObject*
        {
            return script.Get();
        }

        auto state() -> Kh02OpeningFollowupState&
        {
            return observation;
        }

      private:
        RC::Unreal::FWeakObjectPtr matinee, script;
        Kh02OpeningFollowupState observation;
    };
} // namespace kh3ap
