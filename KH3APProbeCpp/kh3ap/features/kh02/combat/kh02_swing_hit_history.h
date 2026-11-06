#pragma once
#include <cstdint>
#include <set>

namespace kh3ap
{
    class Kh02SwingHitHistory
    {
      public:
        void begin()
        {
            hit.clear();
            before_swap.clear();
        }

        void resume()
        {
            before_swap = hit;
        }

        auto blocked(uint64_t victim) const -> bool
        {
            return before_swap.contains(victim);
        }

        void record(uint64_t victim)
        {
            hit.insert(victim);
        }

      private:
        std::set<uint64_t> hit;
        std::set<uint64_t> before_swap;
    };
} // namespace kh3ap
