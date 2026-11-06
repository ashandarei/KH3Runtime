#include "kh3ap/features/music/audio/music_decode_queue.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <latch>
#include <semaphore>
#include <stdexcept>
#include <vector>

using Queue = kh3ap::MusicDecodeQueue<std::optional<int>, 3>;
using namespace std::chrono_literals;

int main()
{
    struct Context
    {
        std::latch entered{1}, release{1};
        std::mutex mutex;
        std::condition_variable changed;
        int notices{};
        std::vector<int> decoded;
    };
    const auto context = std::make_shared<Context>();
    Queue queue{
        [context](const std::filesystem::path& path, std::stop_token stop) -> std::optional<int>
        {
            const int value = std::stoi(path.string());
            if (value == 0)
            {
                context->entered.count_down();
                context->release.wait();
                assert(stop.stop_requested());
            }
            context->decoded.push_back(value);
            if (value < 0)
            {
                throw std::runtime_error("decode failure");
            }
            return value;
        },
        [context]
        {
            const std::lock_guard lock{context->mutex};
            ++context->notices;
            context->changed.notify_all();
        }};
    auto active = queue.submit(0, "0");
    context->entered.wait();
    active->cancel();
    std::shared_ptr<Queue::Completion> latest;
    for (int i = 1; i <= 1000; ++i)
    {
        auto replaced = latest;
        latest = queue.submit(0, std::to_string(i));
        if (replaced)
        {
            assert(replaced->stop_token().stop_requested());
        }
    }
    auto cancelled = queue.submit(1, "1001");
    cancelled->cancel();
    auto other = queue.submit(2, "1002");
    // The blocked first decode excludes every pending job from execution.
    assert(context->decoded.empty());
    context->release.count_down();
    {
        std::unique_lock lock{context->mutex};
        assert(context->changed.wait_for(lock, 5s,
                                         [&]
                                         {
                                             return context->notices == 2;
                                         }));
    }
    assert((context->decoded == std::vector<int>{0, 1002, 1000}));
    assert(!active->take() && !cancelled->take());
    assert(latest->take().value().value() == 1000);
    assert(other->take().value().value() == 1002);
    auto failed = queue.submit(1, "-1");
    {
        std::unique_lock lock{context->mutex};
        assert(context->changed.wait_for(lock, 5s,
                                         [&]
                                         {
                                             return context->notices == 3;
                                         }));
    }
    const auto failure = failed->take();
    assert(failure && !*failure);

    struct Shutdown
    {
        std::latch entered{1};
        std::binary_semaphore release{0};
        std::promise<void> finished;
    };
    auto shutdown = std::make_shared<Shutdown>();
    auto finished = shutdown->finished.get_future();
    {
        Queue retiring{[shutdown](const auto&, std::stop_token stop) -> std::optional<int>
                       {
                           std::stop_callback cancel{stop, [shutdown]
                                                     {
                                                         shutdown->release.release();
                                                     }};
                           shutdown->entered.count_down();
                           shutdown->release.acquire();
                           assert(stop.stop_requested());
                           shutdown->finished.set_value();
                           return 1;
                       },
                       []
                       {
                           assert(false && "cancelled decode must not publish");
                       }};
        retiring.submit(0, "0");
        shutdown->entered.wait();
    }
    assert(finished.wait_for(5s) == std::future_status::ready);
}
