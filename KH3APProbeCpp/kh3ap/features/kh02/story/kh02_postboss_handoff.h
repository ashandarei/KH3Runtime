#pragma once
#include "kh02_postboss_handoff_state.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json_fwd.hpp>

namespace kh3ap
{
    class Kh02PostBossHandoff
    {
      public:
        void bind(RC::Unreal::UObject* actor);
        void observe();
        void clear_for_retry();
        auto dispatch(RC::Unreal::UObject* current_world, RC::Unreal::UObject* source_world, nlohmann::json& proof)
            -> bool;

        auto matinee_binding() -> RC::Unreal::FWeakObjectPtr&
        {
            return matinee;
        }

        Kh02PostBossHandoffState state;

      private:
        RC::Unreal::FWeakObjectPtr matinee;
    };
} // namespace kh3ap
