#pragma once

#include <atomic>
#include <cstdint>

namespace kh3ap
{
    struct RuntimeRetryPointProbeSnapshot
    {
        uint64_t create_pre_hits{0};
        uint64_t create_post_hits{0};
        uint64_t discard_pre_hits{0};
        uint64_t discard_post_hits{0};

        [[nodiscard]] auto empty() const noexcept -> bool
        {
            return create_pre_hits == 0 &&
                   create_post_hits == 0 &&
                   discard_pre_hits == 0 &&
                   discard_post_hits == 0;
        }

        [[nodiscard]] auto same_as(const RuntimeRetryPointProbeSnapshot& other) const noexcept -> bool
        {
            return create_pre_hits == other.create_pre_hits &&
                   create_post_hits == other.create_post_hits &&
                   discard_pre_hits == other.discard_pre_hits &&
                   discard_post_hits == other.discard_post_hits;
        }
    };

    // The hooked callbacks must remain allocation-free and must not inspect any
    // Unreal callback state. Cumulative counters also make a failed sidecar
    // write naturally retryable without clearing an observed event.
    class RuntimeRetryPointProbeState
    {
    public:
        static_assert(std::atomic<uint64_t>::is_always_lock_free);

        auto record_create_pre() noexcept -> void
        {
            create_pre_hits_.fetch_add(1, std::memory_order_relaxed);
        }

        auto record_create_post() noexcept -> void
        {
            create_post_hits_.fetch_add(1, std::memory_order_relaxed);
        }

        auto record_discard_pre() noexcept -> void
        {
            discard_pre_hits_.fetch_add(1, std::memory_order_relaxed);
        }

        auto record_discard_post() noexcept -> void
        {
            discard_post_hits_.fetch_add(1, std::memory_order_relaxed);
        }

        [[nodiscard]] auto snapshot() const noexcept -> RuntimeRetryPointProbeSnapshot
        {
            RuntimeRetryPointProbeSnapshot result{};
            result.create_pre_hits = create_pre_hits_.load(std::memory_order_relaxed);
            result.create_post_hits = create_post_hits_.load(std::memory_order_relaxed);
            result.discard_pre_hits = discard_pre_hits_.load(std::memory_order_relaxed);
            result.discard_post_hits = discard_post_hits_.load(std::memory_order_relaxed);
            return result;
        }

    private:
        std::atomic<uint64_t> create_pre_hits_{0};
        std::atomic<uint64_t> create_post_hits_{0};
        std::atomic<uint64_t> discard_pre_hits_{0};
        std::atomic<uint64_t> discard_post_hits_{0};
    };

    // This state is only touched by the normal game-thread update. It advances
    // after the authoritative latest-file write succeeds, so failures retry the
    // same cumulative snapshot and sequence number on a later update.
    class RuntimeRetryPointProbePersistenceState
    {
    public:
        [[nodiscard]] auto should_persist(const RuntimeRetryPointProbeSnapshot& snapshot) const noexcept -> bool
        {
            return !snapshot.empty() &&
                   (!has_persisted_snapshot_ || !snapshot.same_as(last_persisted_snapshot_));
        }

        [[nodiscard]] auto next_sequence() const noexcept -> uint64_t
        {
            return persisted_sequence_ + 1;
        }

        auto mark_persisted(const RuntimeRetryPointProbeSnapshot& snapshot) noexcept -> void
        {
            last_persisted_snapshot_ = snapshot;
            has_persisted_snapshot_ = true;
            ++persisted_sequence_;
        }

    private:
        RuntimeRetryPointProbeSnapshot last_persisted_snapshot_{};
        uint64_t persisted_sequence_{0};
        bool has_persisted_snapshot_{false};
    };
}
