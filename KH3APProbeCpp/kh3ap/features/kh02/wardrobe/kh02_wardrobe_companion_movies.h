#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>
#include <cstddef>

namespace kh3ap
{
    class Kh02WardrobeCompanionMovies
    {
      public:
        Kh02WardrobeCompanionMovies() = default;
        ~Kh02WardrobeCompanionMovies();
        Kh02WardrobeCompanionMovies(const Kh02WardrobeCompanionMovies&) = delete;
        auto operator=(const Kh02WardrobeCompanionMovies&) -> Kh02WardrobeCompanionMovies& = delete;
        auto prepare(RC::Unreal::UObject* pawn) -> bool;
        void show();
        void hide();

        auto cursor_root() const -> RC::Unreal::UObject*
        {
            return root(cursor_index);
        }

      private:
        static constexpr std::size_t companion_count = 2;
        static constexpr std::size_t background_index = 0;
        static constexpr std::size_t cursor_index = 1;
        auto root(std::size_t index) const -> RC::Unreal::UObject*;
        std::array<RC::Unreal::FWeakObjectPtr, companion_count> movies, assets;
        std::array<bool, companion_count> requested{};
        RC::Unreal::FWeakObjectPtr request_pawn;
    };
} // namespace kh3ap
