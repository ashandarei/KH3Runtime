#include "kh3ap/features/ingredients/ingredient_checkpoint_writer.h"
#include "kh3ap/core/performance_build.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace kh3ap
{
    struct IngredientCheckpointWriter::Impl
    {
        static constexpr size_t queue_limit = 64;
        static constexpr auto retry_delay = std::chrono::milliseconds(250);
        Write write;
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::shared_ptr<const Checkpoint>> pending;
        std::vector<Completion> completed;
        std::atomic<bool> completion_ready{};
        bool stopping{};
        uint64_t submitted{};
        std::thread worker;

        explicit Impl(Write callback)
            : write(std::move(callback)), worker(
                                              [this]()
                                              {
                                                  run();
                                              })
        {
        }

        void run()
        {
            std::shared_ptr<const Checkpoint> last_failure;
            for (;;)
            {
                std::unique_lock lock{mutex};
                changed.wait(lock,
                             [&]()
                             {
                                 return stopping || !pending.empty();
                             });
                if (pending.empty())
                {
                    return;
                }
                auto checkpoint = std::move(pending.front());
                pending.pop_front();
                const auto observed_submission = submitted;
                lock.unlock();
                bool committed = false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                const auto started = std::chrono::steady_clock::now();
#endif
                try
                {
                    committed = write(checkpoint->path, checkpoint->payload);
                }
                catch (...)
                {
                }
                uint64_t commit_us = 0;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                commit_us =
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                              std::chrono::steady_clock::now() - started)
                                              .count());
#endif
                lock.lock();
                if (committed || last_failure != checkpoint)
                {
                    completed.push_back({checkpoint, committed, commit_us});
                    completion_ready.store(true, std::memory_order_release);
                }
                if (committed)
                {
                    last_failure.reset();
                    continue;
                }
                last_failure = checkpoint;
                if (stopping)
                {
                    continue;
                }
                const bool superseded = std::any_of(pending.begin(), pending.end(),
                                                    [&](const auto& next)
                                                    {
                                                        return next->path == checkpoint->path;
                                                    });
                if (!superseded)
                {
                    pending.push_back(std::move(checkpoint));
                }
                // Only an unfinished write retries. New data or shutdown wakes
                // this wait immediately; idle workers have no timed wakeups.
                changed.wait_for(lock, retry_delay,
                                 [&]()
                                 {
                                     return stopping || submitted != observed_submission;
                                 });
            }
        }
    };

    IngredientCheckpointWriter::IngredientCheckpointWriter(Write write)
        : impl_(std::make_unique<Impl>(std::move(write)))
    {
    }
    IngredientCheckpointWriter::~IngredientCheckpointWriter()
    {
        stop();
    }

    auto IngredientCheckpointWriter::submit(Checkpoint checkpoint) -> bool
    {
        auto value = std::make_shared<const Checkpoint>(std::move(checkpoint));
        const std::lock_guard lock{impl_->mutex};
        if (impl_->stopping)
        {
            return false;
        }
        const auto existing = std::find_if(impl_->pending.begin(), impl_->pending.end(),
                                           [&](const auto& prior)
                                           {
                                               return prior->path == value->path;
                                           });
        if (existing != impl_->pending.end())
        {
            *existing = std::move(value);
        }
        else
        {
            if (impl_->pending.size() >= Impl::queue_limit)
            {
                return false;
            }
            impl_->pending.push_back(std::move(value));
        }
        ++impl_->submitted;
        impl_->changed.notify_one();
        return true;
    }

    auto IngredientCheckpointWriter::take_completed() -> std::vector<Completion>
    {
        if (!impl_->completion_ready.exchange(false, std::memory_order_acquire))
        {
            return {};
        }
        const std::lock_guard lock{impl_->mutex};
        std::vector<Completion> result;
        result.swap(impl_->completed);
        return result;
    }

    void IngredientCheckpointWriter::stop()
    {
        {
            const std::lock_guard lock{impl_->mutex};
            impl_->stopping = true;
        }
        impl_->changed.notify_one();
        if (impl_->worker.joinable())
        {
            impl_->worker.join();
        }
    }
} // namespace kh3ap
