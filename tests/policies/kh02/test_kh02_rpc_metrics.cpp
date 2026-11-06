#include "kh3ap/features/kh02/transport/kh02_rpc_metrics.h"
#include <cassert>
#include <numeric>
#include <thread>
#include <vector>

int main()
{
    kh02asset::RpcTimingMetrics metrics;
    metrics.record(10, 50, true);
    metrics.record(11, 500'001, false);
    auto totals = metrics.snapshot();
    assert(totals.attempts == 2);
    assert(totals.successful == 1);
    assert(totals.queue_wait_us == 21);
    assert(totals.maximum_queue_wait_us == 11);
    assert(totals.service_us == 500'051);
    assert(totals.maximum_service_us == 500'001);
    assert(totals.queue_histogram[0] == 1);
    assert(totals.queue_histogram[1] == 1);
    assert(totals.service_histogram[1] == 1);
    assert(totals.service_histogram.back() == 1);

    constexpr unsigned worker_count = 4;
    constexpr unsigned reads_per_worker = 1'000;
    std::vector<std::thread> workers;
    for (unsigned worker = 0; worker < worker_count; ++worker)
    {
        workers.emplace_back(
            [&]
            {
                for (unsigned read = 0; read < reads_per_worker; ++read)
                {
                    metrics.record(100, 500, true);
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    totals = metrics.snapshot();
    const auto concurrent_reads = worker_count * reads_per_worker;
    assert(totals.attempts == concurrent_reads + 2);
    assert(totals.successful == concurrent_reads + 1);
    assert(totals.queue_wait_us == concurrent_reads * 100 + 21);
    assert(totals.service_us == concurrent_reads * 500 + 500'051);
    assert(std::accumulate(totals.queue_histogram.begin(), totals.queue_histogram.end(), std::uint64_t{}) ==
           totals.attempts);
    assert(std::accumulate(totals.service_histogram.begin(), totals.service_histogram.end(), std::uint64_t{}) ==
           totals.attempts);

    // Early-return failures and successful reads each publish exactly one
    // observation, keeping failed operations visible in cumulative telemetry.
    kh02asset::RpcTimingMetrics observed;
    {
        kh02asset::RpcTimingObservation failed(observed);
        failed.lock_acquired();
    }
    {
        kh02asset::RpcTimingObservation successful(observed);
        successful.lock_acquired();
        successful.succeeded();
    }
    assert(observed.snapshot().attempts == 2);
    assert(observed.snapshot().successful == 1);
}
