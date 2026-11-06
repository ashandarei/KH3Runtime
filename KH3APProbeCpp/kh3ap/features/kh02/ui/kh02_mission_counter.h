#pragma once
#include <cstdint>
#include "kh02_mission_counter_policy.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02MissionCounter
    {
      public:
        void prepare(RC::Unreal::UObject* counter, RC::Unreal::UObject* hud);
        void set(int value, int limit, bool visible, bool ready);
        void tick();
        void close();
        void invalidate_presentation();
        void reset_world();

        auto dirty() const -> bool
        {
            return presentation.dirty;
        }

        auto visible() const -> bool
        {
            return presentation.visible;
        }

        auto requested() const -> bool
        {
            return presentation.requested;
        }

        auto rendered_count() const -> int
        {
            return rendered;
        }

        auto limit() const -> int
        {
            return maximum;
        }

        auto updates() const -> uint64_t
        {
            return counter_updates;
        }

      private:
        Kh02MissionCounterPresentation presentation;
        int count{}, maximum{kh02_default_mission_counter_limit}, rendered{-1};
        uint64_t counter_updates{};
    };
} // namespace kh3ap
