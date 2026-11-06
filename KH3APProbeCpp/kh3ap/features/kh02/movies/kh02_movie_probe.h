#pragma once
#include "kh02_movie_state.h"
#include <Unreal/FWeakObjectPtr.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Read-only native movie/backend inspection. Does not prepare, pause, stop,
    // or release video slots. The UI manager identity is retained weakly.
    class Kh02NativeMovieProbe
    {
      public:
        void begin(bool enabled);
        void observe(RC::Unreal::UObject* world, bool final_sample = false);
        auto state() -> nlohmann::json;
        auto observing() const -> bool;
        auto samples() const -> uint64_t;
        auto events() const -> const nlohmann::json&;
        void clear_world();

      private:
        RC::Unreal::FWeakObjectPtr movie_manager;
        Kh02NativeMovieState sampling;
    };
} // namespace kh3ap
