#include "kh3ap/diagnostics/retry/retry_blueprint_probe_state.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using kh3ap::RuntimeRetryBlueprintProbePersistenceState;
    using kh3ap::RuntimeRetryBlueprintProbePhase;
    using kh3ap::RuntimeRetryBlueprintProbeRoute;
    using kh3ap::RuntimeRetryBlueprintProbeState;
    using kh3ap::RuntimeRetryBlueprintProbeTarget;

    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(noexcept(std::declval<RuntimeRetryBlueprintProbeState&>().record(
            RuntimeRetryBlueprintProbeRoute::ProcessEvent,
            RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
            RuntimeRetryBlueprintProbePhase::Pre)));

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
        RuntimeRetryBlueprintProbeState state{};
        const auto snapshot = state.snapshot();
        return require(snapshot.empty(), "initial Blueprint snapshot was not empty") &&
               require(snapshot.total_observations == 0, "initial Blueprint ordinal was non-zero");
    }

    auto test_preserves_route_target_phase_and_order() -> bool
    {
        RuntimeRetryBlueprintProbeState state{};
        state.record(
                RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                RuntimeRetryBlueprintProbePhase::Pre);
        state.record(
                RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                RuntimeRetryBlueprintProbePhase::Pre);
        state.record(
                RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                RuntimeRetryBlueprintProbePhase::Post);

        const auto snapshot = state.snapshot();
        const auto& notify_pre = snapshot.at(
                RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                RuntimeRetryBlueprintProbePhase::Pre);
        const auto& logic_pre = snapshot.at(
                RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                RuntimeRetryBlueprintProbePhase::Pre);
        const auto& notify_post = snapshot.at(
                RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                RuntimeRetryBlueprintProbePhase::Post);

        return require(snapshot.total_observations == 3, "total Blueprint observation count was wrong") &&
               require(notify_pre.hits == 1 && notify_pre.last_ordinal == 1, "notify pre ordering was lost") &&
               require(logic_pre.hits == 1 && logic_pre.last_ordinal == 2, "logic pre ordering was lost") &&
               require(notify_post.hits == 1 && notify_post.last_ordinal == 3, "notify post ordering was lost");
    }

    auto test_failed_persistence_remains_pending() -> bool
    {
        RuntimeRetryBlueprintProbeState state{};
        RuntimeRetryBlueprintProbePersistenceState persistence{};
        state.record(
                RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                RuntimeRetryBlueprintProbePhase::Pre);
        const auto snapshot = state.snapshot();

        if (!require(persistence.should_persist(snapshot), "new Blueprint snapshot was not pending") ||
            !require(persistence.next_sequence() == 1, "initial Blueprint persistence sequence was not one") ||
            !require(persistence.should_persist(snapshot), "failed Blueprint write cleared the pending snapshot"))
        {
            return false;
        }

        persistence.mark_persisted(snapshot);
        return require(!persistence.should_persist(snapshot), "persisted Blueprint snapshot remained pending") &&
               require(persistence.next_sequence() == 2, "Blueprint persistence sequence did not advance");
    }

    auto test_concurrent_callbacks_do_not_lose_observations() -> bool
    {
        RuntimeRetryBlueprintProbeState state{};
        constexpr uint64_t thread_count = 4;
        constexpr uint64_t iterations_per_thread = 20000;
        std::vector<std::thread> threads{};
        threads.reserve(thread_count);

        for (uint64_t thread_index = 0; thread_index < thread_count; ++thread_index)
        {
            threads.emplace_back([&state, iterations_per_thread]() {
                for (uint64_t iteration = 0; iteration < iterations_per_thread; ++iteration)
                {
                    state.record(
                            RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                            RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                            RuntimeRetryBlueprintProbePhase::Pre);
                }
            });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }

        const auto snapshot = state.snapshot();
        const auto& observation = snapshot.at(
                RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                RuntimeRetryBlueprintProbePhase::Pre);
        const uint64_t expected = thread_count * iterations_per_thread;
        return require(snapshot.total_observations == expected, "concurrent Blueprint ordinals were lost") &&
               require(observation.hits == expected, "concurrent Blueprint hits were lost") &&
               require(observation.last_ordinal > 0 && observation.last_ordinal <= expected,
                       "concurrent Blueprint last ordinal was invalid");
    }
}

auto main() -> int
{
    if (!test_starts_empty() ||
        !test_preserves_route_target_phase_and_order() ||
        !test_failed_persistence_remains_pending() ||
        !test_concurrent_callbacks_do_not_lose_observations())
    {
        return 1;
    }

    std::cout << "runtime_retry_blueprint_probe_state tests passed\n";
    return 0;
}
