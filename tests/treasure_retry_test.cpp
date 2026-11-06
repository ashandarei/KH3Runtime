#include "kh3ap/features/treasure/treasure_retry_queues.h"
#include "kh3ap/features/treasure/treasure_retry_processing.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <cassert>
#include <tuple>
#include <vector>

// Mock native weak assignment only; compile the real queue implementation.
namespace kh3ap
{
    auto make_uobject_weak(RC::Unreal::UObject* object) -> RC::Unreal::FWeakObjectPtr
    {
        RC::Unreal::FWeakObjectPtr weak;
        weak = object;
        return weak;
    }
} // namespace kh3ap

namespace
{
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    void queue_boundaries_and_identity()
    {
        RC::Unreal::UObject first;
        RC::Unreal::UObject second;
        RC::Unreal::UObject replacement;
        kh3ap::TreasureStateRetryQueue states;
        kh3ap::TreasureLogicActorRetryQueue actors;
        const auto now = Clock::time_point{};
        assert(states.schedule(&first, L"first", 0, 100ms, 2, now).pending_count == 1);
        assert(!states.schedule(&second, L"second", 2, 200ms, 2, now).dropped_oldest);
        assert(states.pop_due(now + 99ms).empty());
        auto due = states.pop_due(now + 100ms);
        assert(due.size() == 1 && due[0].object.Get() == &first);
        assert(due[0].source_label == L"first" && due[0].attempts == 0);
        assert(states.size() == 1 && actors.size() == 0);
        actors.schedule(&first, L"actor", 0, 10ms, 2, now);
        assert(states.size() == 1 && actors.size() == 1);
        states.schedule(&first, L"new first", 0, 300ms, 2, now);
        const auto overflow = states.schedule(&replacement, L"replacement", 1, 400ms, 2, now);
        assert(overflow.dropped_oldest && overflow.pending_count == 2);
        due = states.pop_due(now + 500ms);
        assert(due.size() == 2 && due[0].source_label == L"new first");
        assert(due[1].source_label == L"replacement" && due[1].attempts == 1);
        assert(states.size() == 0);

        // Reusing a UObject address must not revive the queued old identity.
        ++first.serial;
        due = actors.pop_due(now + 100ms);
        assert(due.size() == 1 && !due[0].object.Get());
        first.serial = 0;
        states.schedule(&first, L"no serial", 0, 0ms, 2, now);
        due = states.pop_due(now);
        assert(due.size() == 1 && !due[0].object.Get());
    }

    struct Probe
    {
        int* object{};
        std::wstring source_label;
        int attempts{};
    };

    void processing_policy(int max_attempts)
    {
        int successful = 1;
        int pending = 2;
        int exhausted = 3;
        std::vector<Probe> probes{{nullptr, L"stale", 0},
                                  {&successful, L"success", 0},
                                  {&pending, L"pending", 0},
                                  {&exhausted, L"exhausted", max_attempts - 1}};
        std::vector<std::wstring> attempts;
        std::vector<std::wstring> exhausted_sources;
        std::vector<std::tuple<int*, std::wstring, int>> rescheduled;
        kh3ap::treasure_retry::drain(
            probes, max_attempts,
            [](const Probe& probe)
            {
                return probe.object;
            },
            [&](int* object, const std::wstring& source)
            {
                attempts.push_back(source);
                return object == &successful;
            },
            [&](int* object, const std::wstring& source)
            {
                assert(object == &exhausted);
                exhausted_sources.push_back(source);
            },
            [&](int* object, const std::wstring& source, int count)
            {
                rescheduled.emplace_back(object, source, count);
            });
        assert((attempts ==
                std::vector<std::wstring>{L"success retry[1]", L"pending retry[1]",
                                          std::format(L"exhausted retry[{}]", max_attempts)}));
        assert((exhausted_sources == std::vector<std::wstring>{L"exhausted"}));
        assert(rescheduled.size() == 1);
        assert(std::get<0>(rescheduled[0]) == &pending);
        assert(std::get<1>(rescheduled[0]) == L"pending" && std::get<2>(rescheduled[0]) == 1);
    }
} // namespace

int main()
{
    queue_boundaries_and_identity();
    processing_policy(4);
    processing_policy(2);
}
