#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Game-thread owner of imported gimmick lock-on suppression. Retained
    // actors/components are weak; native adapters only request invalidation.
    class Kh02StoryTargets
    {
      public:
        void watch(RC::Unreal::UObject* actor);
        void request_sync(RC::Unreal::UObject* changed = nullptr);
        void tick(RC::Unreal::UObject* world);
        auto snapshot() const -> nlohmann::json;
        // Drop world references while retaining bounded diagnostic history.
        void clear_world();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto visibility_fixture(RC::Unreal::UObject* world, const std::function<void()>& observe_world)
            -> nlohmann::json;
#endif

      private:
        struct Target
        {
            RC::Unreal::FWeakObjectPtr component;
            bool suppressed{};
        };

        struct Actor
        {
            RC::Unreal::FWeakObjectPtr actor;
            bool discovered{};
            std::vector<Target> targets;
            bool dirty{true};
        };

        void sync(Actor& item);
        std::vector<Actor> story_targets;
        RC::Unreal::FWeakObjectPtr target_world;
        bool story_targets_pending{};
        uint64_t target_notifications{};
        uint64_t target_syncs{};
        nlohmann::json target_events = nlohmann::json::array();
        std::string target_error;
    };
} // namespace kh3ap
