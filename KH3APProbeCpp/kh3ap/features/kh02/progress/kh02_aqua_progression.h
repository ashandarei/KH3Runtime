#pragma once
#include "kh02_aqua_level_policy.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <optional>

namespace kh3ap
{
    class Kh02StoryFlags;
    class Kh02EntryAssets;

    // The source flag bank owns XP. This caches only the curve and the currently
    // bound pawn; native save/load revisions select the corresponding XP bank.
    class Kh02AquaProgression
    {
      public:
        bool prepare(RC::Unreal::UObject* context, Kh02EntryAssets& assets);
        void bind(RC::Unreal::UObject* pawn, uint64_t revision, Kh02StoryFlags& flags);
        void award(RC::Unreal::UObject* pawn, void* save_owner, int amount, int actual_level, int sora_battle_level,
                   Kh02StoryFlags& flags);
        auto sora_level(void* save_owner) const -> int;
        void refresh_sora(RC::Unreal::UObject* pawn);
        auto experience(RC::Unreal::UObject* pawn) const -> std::optional<int>;

      private:
        void apply(RC::Unreal::UObject* pawn, int total);
        kh02_level::Curve curve{};
        bool curve_ready{};
        std::array<bool, 3> requested{};
        std::array<int, 99> sora_thresholds{};
        std::array<int, 99> enemy_base_exp{};
        RC::Unreal::FWeakObjectPtr sora_refresh_owner;
        RC::Unreal::FWeakObjectPtr hero;
        RC::Unreal::FWeakObjectPtr parameters;
        uint64_t binding_revision{UINT64_MAX};
        uint64_t observed_level_revision{UINT64_MAX};
        int total_exp{kh02_level::initial_exp};
    };
} // namespace kh3ap
