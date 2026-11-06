#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02StoryFlags;

    // Game-thread notifications only enqueue weak identities. Reconciliation
    // waits until the source call stack has returned and progress is ready.
    class Kh02ChestObserver
    {
      public:
        void notify(RC::Unreal::UObject* chest) noexcept;
        void mapset_finished(RC::Unreal::UObject* world) noexcept;
        auto pending() const -> bool;
        void reconcile(RC::Unreal::UObject* world, Kh02StoryFlags& source_flags);
        auto snapshot() const -> nlohmann::json;
        auto error() const -> const std::string&;
        // Retain diagnostic counters/error, but release queued world identities.
        void clear_world();

      private:
        void enqueue(RC::Unreal::UObject* chest);
        std::vector<RC::Unreal::FWeakObjectPtr> pending_chests;
        std::vector<RC::Unreal::FWeakObjectPtr> watched_chests;
        uint64_t notifications{};
        uint64_t reconciliations{};
        uint64_t restored_appearances{};
        std::string last_error;
    };
} // namespace kh3ap
