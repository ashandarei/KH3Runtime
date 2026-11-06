#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <chrono>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Owns temporary native action/walk holds used by diagnostic routes.
    // Cancellation releases both channels independently; destruction calls no engine API.
    class Kh02TimedInputs
    {
      public:
        static constexpr auto default_walk_duration = std::chrono::seconds(10);
        void press(RC::Unreal::UObject* controller, std::wstring action);
        void begin_walk(RC::Unreal::UObject* controller, RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, float x,
                        float y, std::chrono::milliseconds duration = default_walk_duration,
                        bool stop_at_castle_scene = false);
        auto action_active() const -> bool;
        auto walk_active() const -> bool;
        auto walk_stops_at_castle_scene() const -> bool;
        void tick_action();
        void tick_walk(RC::Unreal::UObject* pawn, bool castle_scene_started);
        void cancel();

      private:
        RC::Unreal::FWeakObjectPtr action_controller;
        std::wstring held_action = L"interact";
        std::chrono::steady_clock::time_point release_at{};
        RC::Unreal::FWeakObjectPtr walk_controller, walk_pawn, walk_world;
        float walk_x{}, walk_y{};
        bool castle_trigger{};
        std::chrono::steady_clock::time_point walk_until{};
    };
} // namespace kh3ap
