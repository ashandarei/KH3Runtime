#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <vector>

namespace kh3ap
{
    class Kh02EventAquaScenes
    {
      public:
        void track(RC::Unreal::UObject* matinee, RC::Unreal::UObject* event_pawn);
        void tick();
        void cancel() noexcept;
        auto snapshot() const -> nlohmann::json;

        auto needs_frame() const -> bool
        {
            return !event_aqua_scenes.empty();
        }

        auto playing() const -> bool
        {
            return event_aqua_scene_observed;
        }

      private:
        struct EventAquaScene
        {
            RC::Unreal::FWeakObjectPtr matinee;
            bool started{};
        };

        std::vector<EventAquaScene> event_aqua_scenes;
        RC::Unreal::FWeakObjectPtr event_aqua_owner;
        bool event_aqua_scene_observed{};
        std::uint64_t event_aqua_cleanups{};
    };
} // namespace kh3ap
