#pragma once
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    class Kh02MirrorHorizon
    {
      public:
        auto prepare(RC::Unreal::UObject* pawn) -> bool;

        void reset()
        {
            within_horizon = {};
        }

        auto material() const -> RC::Unreal::UObject*
        {
            return within_horizon.Get();
        }

      private:
        RC::Unreal::FWeakObjectPtr within_horizon;
    };
} // namespace kh3ap
