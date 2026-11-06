#pragma once
#include <memory>
#include <string>
#include <functional>
#include <utility>
#include "kh02_battle_level_policy.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Adapts only the private imported enemy tables, before native pawn creation.
    class Kh02EnemyStats
    {
      public:
        explicit Kh02EnemyStats(std::function<Kh02BattleLevelSettings()> settings);
        ~Kh02EnemyStats();
        bool prepare(RC::Unreal::UObject* context);
        void tick_hero(RC::Unreal::UObject* hero);
        void release_hero(RC::Unreal::UObject* context);
        void set_active(bool active);
        auto experience_levels(RC::Unreal::UObject* hero, int sora_level) -> std::pair<int, int>;
        std::string snapshot() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
