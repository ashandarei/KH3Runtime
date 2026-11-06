#pragma once
#include <nlohmann/json.hpp>
#include <chrono>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02IntroPresentation
    {
      public:
        void set_hud_hidden(bool hidden);
        auto hud_hidden() const -> bool;
        auto fade(RC::Unreal::UObject* context) -> nlohmann::json;
        auto finish(RC::Unreal::UObject* pawn, bool source_finished, float maximum_position) -> bool;
        void reset_cleanup();
        auto cleanup_snapshot() const -> const nlohmann::json&;

      private:
        bool hidden{}, cleanup_requested{};
        nlohmann::json cleanup = nlohmann::json::object();
        std::chrono::steady_clock::time_point cleanup_since{};
    };
} // namespace kh3ap
