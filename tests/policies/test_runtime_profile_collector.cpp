#include "kh3ap/diagnostics/profiling/runtime_profile_collector.h"

#include <cassert>
#include <thread>

using namespace kh3ap;
using namespace std::chrono_literals;
using Clock = RuntimeProfileCollector::Clock;

int main()
{
    const Clock::time_point start{};
    RuntimeProfileCollector collector{25000, 1s, 5s, start};
    assert(!collector.record(L"one_shot", 73, start));
    assert(collector.flush_due(start + 4999ms).empty());
    auto reports = collector.flush_due(start + 5s);
    assert(reports.size() == 1);
    assert(reports[0].scope_name == L"one_shot");
    assert(reports[0].count == 1 && reports[0].total_us == 73);
    assert(reports[0].max_us == 73 && reports[0].last_us == 73);
    assert(reports[0].interval_ms == 5000);
    // An idle scope must never produce a stale or zero-count report.
    assert(collector.flush_due(start + 10s).empty());

    assert(!collector.record(L"one_shot", 10, start + 11s));
    assert(!collector.record(L"one_shot", 30, start + 12s));
    assert(!collector.record(L"late_scope", 0, start + 14999ms));
    reports = collector.flush_due(start + 15s);
    assert(reports.size() == 2);
    for (const auto& report : reports)
    {
        if (report.scope_name == L"one_shot")
        {
            assert(report.count == 2 && report.total_us / report.count == 20);
            assert(report.max_us == 30 && report.last_us == 30);
            assert(report.interval_ms == 4000);
        }
        else
        {
            assert(report.scope_name == L"late_scope");
            assert(report.count == 1 && report.total_us == 0);
            assert(report.interval_ms == 1);
        }
    }
    assert(collector.flush_due(start + 15s).empty());

    // Runtime and reflection keep their distinct warning policies.
    RuntimeProfileCollector runtime{25000, 1s, 5s, start};
    assert(!runtime.record(L"slow", 24999, start));
    assert(runtime.record(L"slow", 25000, start));
    assert(!runtime.record(L"slow", 30000, start + 999ms));
    assert(runtime.record(L"slow", 30000, start + 1s));
    assert(runtime.record(L"slow", 30000, start + 4900ms));
    assert(runtime.flush_due(start + 5s).size() == 1);
    assert(!runtime.record(L"slow", 30000, start + 5100ms));
    assert(runtime.record(L"other_scope", 30000, start + 5100ms));
    assert(runtime.record(L"slow", 30000, start + 5900ms));
    RuntimeProfileCollector reflection{50000, 0s, 5s, start};
    assert(!reflection.record(L"slow", 49999, start));
    assert(reflection.record(L"slow", 50000, start));
    assert(reflection.record(L"slow", 50000, start));

    // Recording and flushing concurrently must neither lose nor duplicate samples.
    RuntimeProfileCollector concurrent{1000000, 0s, 5s, start};
    std::atomic<bool> begin{};
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i)
        producers.emplace_back([&, i] {
            while (!begin.load()) std::this_thread::yield();
            for (int n = 0; n < 2000; ++n)
                concurrent.record(L"shared", 10 + i, start + 1s);
        });
    uint64_t count{}, total{};
    const auto accumulate = [&](const auto& batch) {
        for (const auto& report : batch)
        {
            assert(report.count > 0);
            count += report.count;
            total += report.total_us;
        }
    };
    begin.store(true);
    for (int i = 1; i <= 20; ++i)
        accumulate(concurrent.flush_due(start + i * 5s));
    for (auto& producer : producers) producer.join();
    accumulate(concurrent.flush_due(start + 105s));
    assert(count == 8000);
    assert(total == 2000 * (10 + 11 + 12 + 13));
    assert(concurrent.flush_due(start + 110s).empty());
}
