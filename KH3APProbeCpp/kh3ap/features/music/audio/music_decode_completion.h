#pragma once

#include <atomic>
#include <optional>
#include <utility>
#include <stop_token>

namespace kh3ap
{
    // One worker publishes once. Its shared token outlives cancellation and
    // contains no player/channel pointer. The consumer checks only on events.
    template<class T> class MusicDecodeCompletion
    {
    public:
        auto cancel() noexcept -> void
        {
            cancelled_.request_stop();
        }
        auto stop_token() const noexcept -> std::stop_token
        {
            return cancelled_.get_token();
        }
        template<class Notify> auto publish(T value, Notify notify) -> void
        {
            if (cancelled_.stop_requested())
            {
                return;
            }
            value_ = std::move(value);
            ready_.store(true, std::memory_order_release);
            if (!cancelled_.stop_requested())
            {
                notify();
            }
        }
        auto take() -> std::optional<T>
        {
            if (!ready_.exchange(false, std::memory_order_acquire)) return std::nullopt;
            return std::move(value_);
        }
    private:
        std::atomic_bool ready_{};
        std::stop_source cancelled_;
        std::optional<T> value_{};
    };
}
