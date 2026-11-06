#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace kh3ap
{
    enum class RuntimeRetryBlueprintProbeRoute : size_t
    {
        ProcessEvent = 0,
        ProcessInternal = 1,
    };

    enum class RuntimeRetryBlueprintProbeTarget : size_t
    {
        RetryAnimNotify = 0,
        RetryLogicBeginPlay = 1,
    };

    enum class RuntimeRetryBlueprintProbePhase : size_t
    {
        Pre = 0,
        Post = 1,
    };

    struct RuntimeRetryBlueprintProbeObservation
    {
        uint64_t hits{0};
        uint64_t last_ordinal{0};

        [[nodiscard]] auto same_as(const RuntimeRetryBlueprintProbeObservation& other) const noexcept -> bool
        {
            return hits == other.hits && last_ordinal == other.last_ordinal;
        }
    };

    struct RuntimeRetryBlueprintProbeSnapshot
    {
        static constexpr size_t observation_count = 8;

        uint64_t total_observations{0};
        std::array<RuntimeRetryBlueprintProbeObservation, observation_count> observations{};

        [[nodiscard]] static constexpr auto index(
                RuntimeRetryBlueprintProbeRoute route,
                RuntimeRetryBlueprintProbeTarget target,
                RuntimeRetryBlueprintProbePhase phase) noexcept -> size_t
        {
            return static_cast<size_t>(route) * 4 +
                   static_cast<size_t>(target) * 2 +
                   static_cast<size_t>(phase);
        }

        [[nodiscard]] auto at(
                RuntimeRetryBlueprintProbeRoute route,
                RuntimeRetryBlueprintProbeTarget target,
                RuntimeRetryBlueprintProbePhase phase) const noexcept
                -> const RuntimeRetryBlueprintProbeObservation&
        {
            return observations[index(route, target, phase)];
        }

        [[nodiscard]] auto empty() const noexcept -> bool
        {
            return total_observations == 0;
        }

        [[nodiscard]] auto same_as(const RuntimeRetryBlueprintProbeSnapshot& other) const noexcept -> bool
        {
            if (total_observations != other.total_observations)
            {
                return false;
            }
            for (size_t index_value = 0; index_value < observations.size(); ++index_value)
            {
                if (!observations[index_value].same_as(other.observations[index_value]))
                {
                    return false;
                }
            }
            return true;
        }
    };

    // Hot callbacks call only record(). It performs lock-free relaxed atomic
    // operations and never inspects Unreal objects, allocates, logs, clocks, or
    // performs I/O. Route-specific counters must remain separate because one
    // Blueprint invocation can pass through both ProcessEvent and
    // ProcessInternal.
    class RuntimeRetryBlueprintProbeState
    {
    public:
        static_assert(std::atomic<uint64_t>::is_always_lock_free);

        RuntimeRetryBlueprintProbeState() noexcept
        {
            for (auto& counter : hit_counts_)
            {
                counter.store(0, std::memory_order_relaxed);
            }
            for (auto& ordinal : last_ordinals_)
            {
                ordinal.store(0, std::memory_order_relaxed);
            }
        }

        auto record(
                RuntimeRetryBlueprintProbeRoute route,
                RuntimeRetryBlueprintProbeTarget target,
                RuntimeRetryBlueprintProbePhase phase) noexcept -> void
        {
            const size_t index_value = RuntimeRetryBlueprintProbeSnapshot::index(route, target, phase);
            const uint64_t ordinal = total_observations_.fetch_add(1, std::memory_order_relaxed) + 1;
            hit_counts_[index_value].fetch_add(1, std::memory_order_relaxed);
            last_ordinals_[index_value].store(ordinal, std::memory_order_relaxed);
        }

        [[nodiscard]] auto snapshot() const noexcept -> RuntimeRetryBlueprintProbeSnapshot
        {
            RuntimeRetryBlueprintProbeSnapshot result{};
            result.total_observations = total_observations_.load(std::memory_order_relaxed);
            for (size_t index_value = 0; index_value < result.observations.size(); ++index_value)
            {
                result.observations[index_value].hits =
                        hit_counts_[index_value].load(std::memory_order_relaxed);
                result.observations[index_value].last_ordinal =
                        last_ordinals_[index_value].load(std::memory_order_relaxed);
            }
            return result;
        }

    private:
        std::atomic<uint64_t> total_observations_{0};
        std::array<std::atomic<uint64_t>, RuntimeRetryBlueprintProbeSnapshot::observation_count> hit_counts_{};
        std::array<std::atomic<uint64_t>, RuntimeRetryBlueprintProbeSnapshot::observation_count> last_ordinals_{};
    };

    class RuntimeRetryBlueprintProbePersistenceState
    {
    public:
        [[nodiscard]] auto should_persist(const RuntimeRetryBlueprintProbeSnapshot& snapshot) const noexcept -> bool
        {
            return !snapshot.empty() &&
                   (!has_persisted_snapshot_ || !snapshot.same_as(last_persisted_snapshot_));
        }

        [[nodiscard]] auto next_sequence() const noexcept -> uint64_t
        {
            return persisted_sequence_ + 1;
        }

        auto mark_persisted(const RuntimeRetryBlueprintProbeSnapshot& snapshot) noexcept -> void
        {
            last_persisted_snapshot_ = snapshot;
            has_persisted_snapshot_ = true;
            ++persisted_sequence_;
        }

    private:
        RuntimeRetryBlueprintProbeSnapshot last_persisted_snapshot_{};
        uint64_t persisted_sequence_{0};
        bool has_persisted_snapshot_{false};
    };
}
