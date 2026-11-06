#include "kh3ap/features/kh02/transport/kh02_file_cache.h"
#include <array>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <latch>
#include <mutex>

using namespace std::chrono_literals;
using Output = std::array<std::byte, 4>;

static bool fill(std::span<std::byte> bytes)
{
    std::fill(bytes.begin(), bytes.end(), std::byte{7});
    return true;
}

static bool hit_completes_while_miss_is_blocked(bool unlock_load)
{
    kh02asset::FileReadCache cache(16);
    std::mutex mutex;
    Output initial;
    assert(cache.read(1, 4, 0, initial, fill));
    std::promise<void> entered;
    std::promise<void> release;
    const auto released = release.get_future().share();
    auto miss = std::async(std::launch::async,
                           [&]
                           {
                               Output bytes;
                               std::unique_lock lock(mutex);
                               auto load = [&](std::span<std::byte> value)
                               {
                                   entered.set_value();
                                   released.wait();
                                   return fill(value);
                               };
                               return unlock_load ? cache.read_with_unlocked_load(2, 4, 0, bytes, lock, load)
                                                  : cache.read(2, 4, 0, bytes, load);
                           });
    assert(entered.get_future().wait_for(1s) == std::future_status::ready);
    std::promise<void> trying_hit;
    auto hit =
        std::async(std::launch::async,
                   [&]
                   {
                       Output bytes;
                       trying_hit.set_value();
                       std::unique_lock lock(mutex);
                       return cache.read_with_unlocked_load(1, 4, 0, bytes, lock,
                                                            [](auto)
                                                            {
                                                                assert(false); // A hit must never reach the provider.
                                                                return false;
                                                            });
                   });
    assert(trying_hit.get_future().wait_for(1s) == std::future_status::ready);
    const bool independent = hit.wait_for(unlock_load ? 1s : 100ms) == std::future_status::ready;
    release.set_value();
    assert(miss.get() && hit.get());
    return independent;
}

int main()
{
    assert(!hit_completes_while_miss_is_blocked(false)); // Previous call-site locking serializes a cached hit.
    assert(hit_completes_while_miss_is_blocked(true));

    kh02asset::FileReadCache cache(16);
    std::mutex mutex;
    std::promise<void> entered;
    std::promise<void> release;
    auto released = release.get_future().share();
    Output invalidated{};
    auto miss = std::async(std::launch::async,
                           [&]
                           {
                               std::unique_lock lock(mutex);
                               return cache.read_with_unlocked_load(3, 4, 0, invalidated, lock,
                                                                    [&](auto bytes)
                                                                    {
                                                                        entered.set_value();
                                                                        released.wait();
                                                                        return fill(bytes);
                                                                    });
                           });
    assert(entered.get_future().wait_for(1s) == std::future_status::ready);
    {
        std::lock_guard lock(mutex);
        cache.clear(); // Simulates provider/catalog invalidation or cache policy change during I/O.
    }
    release.set_value();
    assert(!miss.get());
    assert(invalidated == Output{} && cache.stats().bytes == 0 && !cache.contains(3));

    std::latch both_loading(2);
    const auto same_key = [&]
    {
        Output bytes;
        std::unique_lock lock(mutex);
        assert(cache.read_with_unlocked_load(4, 4, 0, bytes, lock,
                                             [&](auto value)
                                             {
                                                 both_loading.arrive_and_wait();
                                                 return fill(value);
                                             }));
        return bytes;
    };
    auto first = std::async(std::launch::async, same_key);
    auto second = std::async(std::launch::async, same_key);
    assert(first.wait_for(1s) == std::future_status::ready && second.wait_for(1s) == std::future_status::ready);
    assert(first.get() == second.get());
    assert(cache.stats().bytes == 4); // Racing misses neither double-count nor duplicate LRU entries.
    {
        std::unique_lock lock(mutex);
        Output bytes;
        try
        {
            cache.read_with_unlocked_load(5, 4, 0, bytes, lock, [](auto) -> bool { throw 42; });
            assert(false);
        }
        catch (int value)
        {
            assert(value == 42 && lock.owns_lock());
        }
    }
    std::cout << "Cached hit completed before blocked miss release; old locking blocked it. "
                 "Invalidation, duplicate fill and exception locking passed.\n";
}
