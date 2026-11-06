#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Reconciles the authored Castle Town shooting-star trigger after hero
    // replacement. Story progression supplies completion and readiness gates.
    class Kh02ShootingStar
    {
      public:
        void tick(RC::Unreal::UObject* pawn, bool challenge_complete);
        void clear_world();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto snapshot(RC::Unreal::UObject* pawn, int challenge_state) const -> nlohmann::json;
#endif

      private:
        void reconcile(RC::Unreal::UObject* pawn, bool challenge_complete);
        RC::Unreal::FWeakObjectPtr hero;
        uint64_t hero_arrived_at{};
        bool hero_checked{};
        nlohmann::json handoff = nlohmann::json::object();
    };
} // namespace kh3ap
