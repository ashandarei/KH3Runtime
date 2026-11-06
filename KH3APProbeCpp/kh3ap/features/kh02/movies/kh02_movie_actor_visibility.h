#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <vector>

namespace kh3ap
{
    class Kh02MovieActorVisibility
    {
      public:
        void collect(RC::Unreal::UObject* matinee, RC::Unreal::UObject* pawn);
        void tick(RC::Unreal::UObject* world);
        void cancel() noexcept;

        auto size() const -> std::size_t
        {
            return ending_movie_hidden_actors.size();
        }

        auto needs_frame() const -> bool
        {
            return !ending_movie_hidden_actors.empty();
        }

      private:
        struct HiddenActor
        {
            RC::Unreal::FWeakObjectPtr actor;
            bool originally_hidden{};
        };

        std::vector<HiddenActor> ending_movie_hidden_actors;
        std::vector<HiddenActor> retired_movie_hidden_actors;
    };
} // namespace kh3ap
