#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <chrono>

namespace kh3ap
{
    class Kh02CollapsePresentation
    {
      public:
        void bind(RC::Unreal::UObject* matinee, RC::Unreal::UObject* script);
        void tick(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, RC::Unreal::UObject* event_aqua,
                  std::string& error);
        void restore() noexcept;
        void reset();

        auto actor() const -> RC::Unreal::UObject*
        {
            return collapse_matinee.Get();
        }

        auto needs_frame() const -> bool;
        auto snapshot() const -> nlohmann::json;

      private:
        void finish_view(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world);
        RC::Unreal::FWeakObjectPtr collapse_world;
        bool view_cleanup_pending{}, view_cleanup_started{};
        std::chrono::steady_clock::time_point view_cleanup_since{};
        nlohmann::json view_cleanup_proof = nlohmann::json::object();
        RC::Unreal::FWeakObjectPtr collapse_matinee, collapse_script;
        RC::Unreal::FWeakObjectPtr collapse_hidden_hero;
        bool collapse_hero_was_hidden{}, collapse_binding_checked{}, collapse_binding_verified{};
        nlohmann::json collapse_binding_proof = nlohmann::json::object();
        RC::Unreal::FWeakObjectPtr collapse_movement;
        bool collapse_movement_was_enabled{}, collapse_handoff_completed{}, collapse_observed{};
        nlohmann::json collapse_handoff_proof = nlohmann::json::object();
        float collapse_maximum{};
    };
} // namespace kh3ap
