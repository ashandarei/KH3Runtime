#pragma once
#include <format>
#include <string>

namespace kh3ap::treasure_retry
{
    template <class Entries, class Resolve, class Process, class Exhausted, class Reschedule>
    auto drain(Entries& entries, int max_attempts, Resolve&& resolve, Process&& process,
               Exhausted&& exhausted, Reschedule&& reschedule) -> void
    {
        for (auto& probe : entries)
        {
            auto* object = resolve(probe);
            if (!object)
            {
                continue;
            }
            const int next_attempt = probe.attempts + 1;
            const std::wstring retry_source =
                std::format(L"{} retry[{}]", probe.source_label, next_attempt);
            if (process(object, retry_source))
            {
                continue;
            }
            if (next_attempt >= max_attempts)
            {
                exhausted(object, probe.source_label);
                continue;
            }
            reschedule(object, probe.source_label, next_attempt);
        }
    }
} // namespace kh3ap::treasure_retry
