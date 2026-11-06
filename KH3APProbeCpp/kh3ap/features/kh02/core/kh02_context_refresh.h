#pragma once
#include <chrono>
#include <cstdint>

namespace kh3ap
{
    // Native map/controller events arm work. Only an unfinished refresh retries;
    // a settled context has no periodic deadline or UObject discovery.
    class Kh02ContextRefresh
    {
      public:
        void observe(uint64_t revision)
        {
            if (!observed || revision != context_revision)
            {
                observed = true;
                context_revision = revision;
                pending = true;
                retry_at = {};
            }
        }

        auto due(std::chrono::steady_clock::time_point now) const -> bool
        {
            return pending && now >= retry_at;
        }

        void complete(bool settled, std::chrono::steady_clock::time_point now)
        {
            pending = !settled;
            retry_at = settled ? std::chrono::steady_clock::time_point{} : now + retry_interval;
        }

      private:
        static constexpr auto retry_interval = std::chrono::milliseconds(250);
        uint64_t context_revision{};
        bool observed{};
        bool pending{};
        std::chrono::steady_clock::time_point retry_at{};
    };
} // namespace kh3ap
