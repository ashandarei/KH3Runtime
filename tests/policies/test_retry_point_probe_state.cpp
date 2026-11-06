#include "kh3ap/diagnostics/retry/retry_point_probe_state.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using kh3ap::RuntimeRetryPointProbePersistenceState;
    using kh3ap::RuntimeRetryPointProbeState;

    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(noexcept(std::declval<RuntimeRetryPointProbeState&>().record_create_pre()));
    static_assert(noexcept(std::declval<RuntimeRetryPointProbeState&>().record_create_post()));
    static_assert(noexcept(std::declval<RuntimeRetryPointProbeState&>().record_discard_pre()));
    static_assert(noexcept(std::declval<RuntimeRetryPointProbeState&>().record_discard_post()));

    auto require(bool condition, const char* message) -> bool
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            return false;
        }
        return true;
    }

    auto test_starts_empty() -> bool
    {
        RuntimeRetryPointProbeState state{};
        const auto snapshot = state.snapshot();
        return require(snapshot.empty(), "initial snapshot was not empty") &&
               require(snapshot.create_pre_hits == 0, "initial Create pre count was non-zero") &&
               require(snapshot.create_post_hits == 0, "initial Create post count was non-zero") &&
               require(snapshot.discard_pre_hits == 0, "initial Discard pre count was non-zero") &&
               require(snapshot.discard_post_hits == 0, "initial Discard post count was non-zero");
    }

    auto test_preserves_every_kind_and_phase() -> bool
    {
        RuntimeRetryPointProbeState state{};
        state.record_create_pre();
        state.record_create_pre();
        state.record_create_post();
        state.record_discard_pre();
        state.record_discard_post();
        state.record_discard_post();
        state.record_discard_post();

        const auto snapshot = state.snapshot();
        return require(snapshot.create_pre_hits == 2, "Create pre count was not cumulative") &&
               require(snapshot.create_post_hits == 1, "Create post count was not cumulative") &&
               require(snapshot.discard_pre_hits == 1, "Discard pre count was not cumulative") &&
               require(snapshot.discard_post_hits == 3, "Discard post count was not cumulative");
    }

    auto test_failed_persistence_remains_pending() -> bool
    {
        RuntimeRetryPointProbeState state{};
        RuntimeRetryPointProbePersistenceState persistence{};
        state.record_create_pre();
        state.record_create_post();
        const auto snapshot = state.snapshot();

        if (!require(persistence.should_persist(snapshot), "new snapshot was not pending") ||
            !require(persistence.next_sequence() == 1, "initial persistence sequence was not one"))
        {
            return false;
        }

        // Simulate a failed latest-file write by deliberately not marking the
        // snapshot persisted.
        if (!require(persistence.should_persist(snapshot), "failed write cleared the pending snapshot") ||
            !require(persistence.next_sequence() == 1, "failed write advanced the persistence sequence"))
        {
            return false;
        }

        persistence.mark_persisted(snapshot);
        if (!require(!persistence.should_persist(snapshot), "persisted snapshot remained pending") ||
            !require(persistence.next_sequence() == 2, "successful write did not advance the sequence"))
        {
            return false;
        }

        state.record_create_pre();
        return require(persistence.should_persist(state.snapshot()), "new counter value was not pending");
    }

    auto test_concurrent_callbacks_do_not_lose_increments() -> bool
    {
        RuntimeRetryPointProbeState state{};
        constexpr uint64_t thread_count = 4;
        constexpr uint64_t iterations_per_thread = 20000;
        std::vector<std::thread> threads{};
        threads.reserve(thread_count);

        for (uint64_t thread_index = 0; thread_index < thread_count; ++thread_index)
        {
            threads.emplace_back([&state, iterations_per_thread]() {
                for (uint64_t iteration = 0; iteration < iterations_per_thread; ++iteration)
                {
                    state.record_create_pre();
                    state.record_create_post();
                }
            });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }

        const auto snapshot = state.snapshot();
        const uint64_t expected = thread_count * iterations_per_thread;
        return require(snapshot.create_pre_hits == expected, "concurrent Create pre increments were lost") &&
               require(snapshot.create_post_hits == expected, "concurrent Create post increments were lost") &&
               require(snapshot.discard_pre_hits == 0, "concurrent test changed Discard pre") &&
               require(snapshot.discard_post_hits == 0, "concurrent test changed Discard post");
    }
}

auto main() -> int
{
    if (!test_starts_empty() ||
        !test_preserves_every_kind_and_phase() ||
        !test_failed_persistence_remains_pending() ||
        !test_concurrent_callbacks_do_not_lose_increments())
    {
        return 1;
    }

    std::cout << "runtime_retry_point_probe_state tests passed\n";
    return 0;
}
