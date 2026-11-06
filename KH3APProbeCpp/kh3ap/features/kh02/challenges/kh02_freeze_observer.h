#pragma once
#include "kh02_challenge_policy.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <map>
#include <chrono>

namespace kh3ap
{
    class Kh02FreezeObserver
    {
      public:
        void observe(RC::Unreal::UObject* enemy, uint64_t identity, int element, bool was_frozen,
                     std::map<int, int>& states, std::map<int, int>& counts, bool& pending_change);

        void tick();

        void reset()
        {
            freeze_cohorts.groups.clear();
            frozen_actors.clear();
            pending_freezes.clear();
        }

        auto group_count() const -> std::size_t
        {
            return freeze_cohorts.groups.size();
        }

      private:
        kh02_tracking::FreezeCohorts freeze_cohorts;
        std::map<uint64_t, RC::Unreal::FWeakObjectPtr> frozen_actors;

        struct PendingFreeze
        {
            RC::Unreal::FWeakObjectPtr actor;
            std::chrono::steady_clock::time_point expires;
        };

        std::map<uint64_t, PendingFreeze> pending_freezes;
        void record_freeze(RC::Unreal::UObject* enemy, uint64_t identity);
    };
} // namespace kh3ap
