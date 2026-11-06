#pragma once
#include "kh3ap/features/story/progression/vbonus_tracking_policy.h"
#include <atomic>
#include <set>
#include <utility>

namespace kh3ap
{
    // Map completion includes every native chest, even when its AP check is
    // handled elsewhere (KH0.2) or the seed does not randomize that chest.
    class ChestOpenEvents
    {
      public:
        struct Batch
        {
            bool changed{};
            std::set<uint64_t> checks;
        };

        void configure(std::set<uint64_t> names)
        {
            checks.configure(std::move(names));
        }

        void write(uint64_t name)
        {
            checks.write(name, true);
            changed.store(true, std::memory_order_release);
        }

        auto pending() const -> bool
        {
            return changed.load(std::memory_order_acquire) || checks.pending();
        }

        auto take() -> Batch
        {
            return {changed.exchange(false, std::memory_order_acq_rel), checks.take()};
        }

        auto events() const -> uint64_t
        {
            return checks.events();
        }

      private:
        VBonusFlagEvents checks;
        std::atomic<bool> changed{};
    };
} // namespace kh3ap
