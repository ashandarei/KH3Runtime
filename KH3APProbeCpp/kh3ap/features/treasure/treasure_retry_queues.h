#pragma once

#include <Unreal/FWeakObjectPtr.hpp>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap
{
    struct DeferredTreasureProbe
    {
        RC::Unreal::FWeakObjectPtr object;
        std::wstring source_label;
        int attempts{};
        std::chrono::steady_clock::time_point target_time{};
    };

    struct TreasureRetryScheduleResult
    {
        bool dropped_oldest{};
        size_t pending_count{};
    };

    class TreasureRetryQueue
    {
    public:
        // Producers run in the game-thread observation drain. Other threads may
        // read queue size; all deferred objects retain serial-checked identities.
        auto schedule(RC::Unreal::UObject* object, std::wstring source_label, int attempts,
                      std::chrono::steady_clock::duration delay, size_t max_pending,
                      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
            -> TreasureRetryScheduleResult;
        auto pop_due(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
            -> std::vector<DeferredTreasureProbe>;
        [[nodiscard]] auto size() const -> size_t;

    private:
        mutable std::mutex mutex_;
        std::vector<DeferredTreasureProbe> pending_;
    };

    // Separate instances keep each discovery route's capacity and retry policy.
    using TreasureStateRetryQueue = TreasureRetryQueue;
    using TreasureLogicActorRetryQueue = TreasureRetryQueue;
} // namespace kh3ap
