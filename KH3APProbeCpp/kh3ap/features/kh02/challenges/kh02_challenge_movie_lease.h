#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include "kh3ap/features/kh02/movies/kh02_menu_visibility.h"

namespace kh3ap
{
    // Owns loading and one-time setup of the private challenge/wardrobe movie.
    class Kh02ChallengeMovieLease
    {
      public:
        Kh02ChallengeMovieLease() = default;
        ~Kh02ChallengeMovieLease();
        Kh02ChallengeMovieLease(const Kh02ChallengeMovieLease&) = delete;
        auto operator=(const Kh02ChallengeMovieLease&) -> Kh02ChallengeMovieLease& = delete;
        auto prepare(RC::Unreal::UObject* pawn) -> bool;
        auto root() const -> RC::Unreal::UObject*;
        auto ready() const -> bool;

        auto player() const -> RC::Unreal::UObject*
        {
            return movie.Get();
        }

        auto load_requested() const -> bool
        {
            return requested;
        }

        void hide_native(RC::Unreal::UObject* camp);
        void hide();

      private:
        auto prepare_menu() -> bool;
        RC::Unreal::FWeakObjectPtr movie, asset, prepared_movie, request_pawn, request_world;
        Kh02NativeMenuVisibility native_visibility;
        bool requested{};
    };
} // namespace kh3ap
