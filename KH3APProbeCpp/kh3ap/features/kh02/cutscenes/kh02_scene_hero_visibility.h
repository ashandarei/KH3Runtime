#pragma once
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    class Kh02SceneHeroVisibility
    {
      public:
        void tick(RC::Unreal::UObject* pawn, bool playing);

      private:
        RC::Unreal::FWeakObjectPtr hidden_hero;
        bool hero_was_hidden{};
    };
} // namespace kh3ap
