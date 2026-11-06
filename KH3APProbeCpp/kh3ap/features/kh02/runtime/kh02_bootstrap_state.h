#pragma once
#include <chrono>
#include <cstdint>

namespace kh3ap
{
    // Native manager result 1 completes a successful load. Result 2 is the
    // inspected unavailable-load branch, including GameOver without a save.
    struct Kh02UnavailableLoadCompletion
    {
        static constexpr int32_t result = 2;
        int32_t slot{};
        const void* metadata{};
    };

    // Pure policy: a retained native request has a bounded lifetime and can be
    // released only after both independent prerequisites have completed.
    class Kh02BootstrapState
    {
      public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto preparation_timeout = std::chrono::seconds(180);
        enum class Phase
        {
            idle,
            waiting,
            ready,
            failed
        };
        enum class Decision
        {
            wait,
            dispatch,
            cancel
        };

        auto begin(Clock::time_point now) -> bool
        {
            if (phase_ == Phase::waiting)
            {
                return false;
            }
            phase_ = Phase::waiting;
            deadline_ = now + preparation_timeout;
            ++generation_;
            return true;
        }

        auto poll(Clock::time_point now, bool owner_valid, bool provider_ready, bool aliases_ready) -> Decision
        {
            if (phase_ != Phase::waiting)
            {
                return Decision::wait;
            }
            if (!owner_valid || now >= deadline_)
            {
                phase_ = Phase::failed;
                return Decision::cancel;
            }
            if (provider_ready && aliases_ready)
            {
                phase_ = Phase::ready;
                return Decision::dispatch;
            }
            return Decision::wait;
        }

        auto fail() -> bool
        {
            const bool pending = phase_ == Phase::waiting;
            phase_ = Phase::failed;
            return pending;
        }

        auto phase() const -> Phase
        {
            return phase_;
        }

        auto generation() const -> uint64_t
        {
            return generation_;
        }

      private:
        Phase phase_{Phase::idle};
        Clock::time_point deadline_{};
        uint64_t generation_{};
    };
} // namespace kh3ap
