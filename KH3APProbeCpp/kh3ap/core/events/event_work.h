#pragma once
#include <atomic>
#include <chrono>
#include <memory>

namespace kh3ap
{
    inline constexpr auto default_event_work_retry_interval = std::chrono::milliseconds(250);

    // Producers only signal. Pending state and retry deadlines belong to the game
    // thread. A notification raised during an attempt survives its completion.
    class RuntimeEventWork
    {
    public:
        using Clock = std::chrono::steady_clock;
        void request() noexcept
        {
            signal_->store(true, std::memory_order_release);
        }
        auto notifier() const
        {
            return [signal = signal_]
            {
                signal->store(true, std::memory_order_release);
            };
        }
        bool pending() const
        {
            return pending_ || signal_->load(std::memory_order_acquire);
        }
        bool take(Clock::time_point now)
        {
            if (signal_->exchange(false, std::memory_order_acq_rel))
            {
                pending_ = true;
                next_ = {};
            }
            if (!pending_ || now < next_)
            {
                return false;
            }
            pending_ = false;
            return true;
        }
        void finish(Clock::time_point now, bool complete,
                    Clock::duration retry = default_event_work_retry_interval)
        {
            if (!complete)
            {
                pending_ = true;
                next_ = now + retry;
            }
        }

    private:
        std::shared_ptr<std::atomic_bool> signal_ = std::make_shared<std::atomic_bool>(true);
        bool pending_{};
        Clock::time_point next_{};
    };
} // namespace kh3ap
