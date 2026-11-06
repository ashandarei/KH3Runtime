#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace kh02asset
{
    // Completion belongs to the task, not the engine request. PollCompletion can
    // release that request as soon as its callback returns, while the worker is
    // still unwinding. The request's destructor waits for this separate fence.
    class ReadTask
    {
      public:
        explicit ReadTask(std::function<void(ReadTask&)> work) : work_(std::move(work)) {}

        void cancel()
        {
            canceled_.store(true, std::memory_order_release);
        }

        bool canceled() const
        {
            return canceled_.load(std::memory_order_acquire);
        }

        bool wait(float seconds = 0)
        {
            std::unique_lock lock(mutex_);
            if (seconds <= 0)
            {
                completed_.wait(lock, [this] { return done_; });
                return true;
            }
            return completed_.wait_for(lock, std::chrono::duration<float>(seconds), [this] { return done_; });
        }

      private:
        friend class AsyncReads;
        std::function<void(ReadTask&)> work_;
        std::atomic<bool> canceled_{};
        std::mutex mutex_;
        std::condition_variable completed_;
        bool done_{};
    };

    class AsyncReads
    {
      public:
        struct Stats
        {
            bool enabled;
            size_t pending, peak_pending;
            uint64_t queued, inline_reads, completed, canceled;
        };

        explicit AsyncReads(size_t capacity = 256) : capacity_(capacity) {}

        AsyncReads(const AsyncReads&) = delete;
        AsyncReads& operator=(const AsyncReads&) = delete;

        ~AsyncReads()
        {
            shutdown();
        }

        void submit(const std::shared_ptr<ReadTask>& task)
        {
            std::unique_lock lock(mutex_);
            ++pending_;
            peak_pending_ = std::max(peak_pending_, pending_);
            // Saturation retains the old synchronous behavior instead of
            // allocating unbounded buffers or waiting for a queue slot inside a
            // reentrant completion callback. Buffers are allocated when run.
            if (stopping_ || !enabled_ || queue_.size() >= capacity_ ||
                (worker_.joinable() && worker_.get_id() == std::this_thread::get_id()))
            {
                ++inline_reads_;
                if (stopping_)
                {
                    task->cancel();
                }
                lock.unlock();
                run(task);
                return;
            }
            try
            {
                if (!worker_.joinable())
                {
                    worker_ = std::thread([this] { consume(); });
                }
                queue_.push_back(task);
            }
            catch (...)
            {
                // Resource pressure must still produce a terminal request.
                ++inline_reads_;
                lock.unlock();
                run(task);
                return;
            }
            ++queued_;
            lock.unlock();
            available_.notify_one();
        }

        bool configure(bool enabled)
        {
            std::lock_guard lock(mutex_);
            if (pending_ || stopping_)
            {
                return false;
            }
            enabled_ = enabled;
            return true;
        }

        Stats stats() const
        {
            std::lock_guard lock(mutex_);
            return {enabled_, pending_, peak_pending_, queued_, inline_reads_, completed_, canceled_};
        }

        void shutdown()
        {
            {
                std::lock_guard lock(mutex_);
                stopping_ = true;
                for (const auto& task : queue_)
                {
                    task->cancel();
                }
            }
            available_.notify_one();
            if (worker_.joinable())
            {
                worker_.join();
            }
            std::unique_lock lock(mutex_);
            idle_.wait(lock, [this] { return pending_ == 0; });
        }

      private:
        void run(const std::shared_ptr<ReadTask>& task)
        {
            // The ABI adapter contains read failures and always completes its
            // callback. No cache/transport/queue lock is held during that call.
            task->work_(*task);
            task->work_ = {};
            {
                std::lock_guard lock(mutex_);
                --pending_;
                ++completed_;
                canceled_ += task->canceled();
                {
                    std::lock_guard task_lock(task->mutex_);
                    task->done_ = true;
                }
                task->completed_.notify_all();
                idle_.notify_all();
            }
        }

        void consume()
        {
            for (;;)
            {
                std::shared_ptr<ReadTask> task;
                {
                    std::unique_lock lock(mutex_);
                    available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                    if (queue_.empty())
                    {
                        return;
                    }
                    task = std::move(queue_.front());
                    queue_.pop_front();
                }
                run(task);
            }
        }

        const size_t capacity_;
        mutable std::mutex mutex_;
        std::condition_variable available_, idle_;
        std::deque<std::shared_ptr<ReadTask>> queue_;
        std::thread worker_;
        bool enabled_ = true, stopping_{};
        size_t pending_{}, peak_pending_{};
        uint64_t queued_{}, inline_reads_{}, completed_{}, canceled_{};
    };
} // namespace kh02asset

namespace kh3ap
{
    auto kh02_async_reads() -> kh02asset::AsyncReads&;
}
