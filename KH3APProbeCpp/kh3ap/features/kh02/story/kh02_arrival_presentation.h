#pragma once
#include "kh02_map_session_state.h"
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    // Native cover and paired input claims. Arrival phases/readiness remain in MapSession.
    class Kh02ArrivalPresentation
    {
      public:
        Kh02ArrivalPresentation() = default;
        Kh02ArrivalPresentation(const Kh02ArrivalPresentation&) = delete;
        auto operator=(const Kh02ArrivalPresentation&) -> Kh02ArrivalPresentation& = delete;
        void bind_context(RC::Unreal::UObject* context);

        auto context() const -> RC::Unreal::UObject*
        {
            return cover_context.Get();
        }

        void cover(RC::Unreal::UObject* context, float amount);
        void reveal(RC::Unreal::UObject* context);
        void reveal_source(RC::Unreal::UObject* context);
        auto changing(RC::Unreal::UObject* context) -> bool;
        void hold_input(RC::Unreal::UObject* pawn);
        void release_input();
        void reset_bindings();

        auto damage_protection_claimed() const -> bool
        {
            return damage_claimed;
        }
        auto damage_protection_restored() const -> bool
        {
            return damage_restoration_verified;
        }

        auto movement_claimed() const -> bool
        {
            return claims.movement;
        }

        static auto loading_closed() -> bool;
        static void open_loading();
        static void close_loading();

      private:
        RC::Unreal::FWeakObjectPtr cover_context, controller;
        RC::Unreal::FWeakObjectPtr protected_pawn, protected_world;
        bool damage_claimed{}, original_no_hp_damage{};
        bool damage_restoration_verified{true};
        void release_damage_protection() noexcept;
        Kh02ArrivalInputClaims claims;
    };
} // namespace kh3ap
