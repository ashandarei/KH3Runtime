#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>

namespace kh3ap
{
    class Kh02EntryAssets;

    class Kh02EventAqua
    {
      public:
        auto preload(RC::Unreal::UObject* pawn, Kh02EntryAssets& assets) -> bool;
        auto prepare(RC::Unreal::UObject* pawn) -> bool;

        auto actor() const -> RC::Unreal::UObject*
        {
            return event_actor.Get();
        }

        void forget_actor()
        {
            event_actor = {};
        }

        void reset_request()
        {
            class_requested = false;
            playable_assets_requested.fill(false);
        }

        void reset()
        {
            forget_actor();
            reset_request();
        }

      private:
        RC::Unreal::FWeakObjectPtr event_actor;
        bool class_requested{};
        std::array<bool, 3> playable_assets_requested{};
    };
} // namespace kh3ap
