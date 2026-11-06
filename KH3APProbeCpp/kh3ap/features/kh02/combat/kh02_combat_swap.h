#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "kh3ap/core/performance_build.h"
#include "kh3ap/features/kh02/core/kh02_context_refresh.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02CombatSwap
    {
      public:
        using Player = std::function<RC::Unreal::UObject*()>;
        using Change = std::function<bool(RC::Unreal::UObject*, int)>;
        Kh02CombatSwap(Player player, Change change);
        ~Kh02CombatSwap();
        bool request(RC::Unreal::UObject* pawn, bool trap_owned = false);
        bool available(RC::Unreal::UObject* pawn) const;
        bool busy() const;
        bool attack_key(int controller, std::wstring_view key, int event);
        auto tick(std::chrono::steady_clock::time_point now) -> std::optional<int>;
        static bool blocks_swing_damage(RC::Unreal::UObject* victim, RC::Unreal::UObject* instigator,
                                        RC::Unreal::UObject* causer, const void* event);
        static void record_swing_damage(RC::Unreal::UObject* victim, RC::Unreal::UObject* instigator,
                                        RC::Unreal::UObject* causer, const void* event);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        bool give_combo_master();
        bool start_test(bool air, bool expire, float phase = 0.35f);
        bool start_input_test(bool swap_back);
        bool start_camera_test();
        bool start_movement_test(bool rotate = false);
        bool start_locomotion_test(int swaps, int warmup_ms, int strength_percent, bool release_at_swap = false,
                                   bool camera_relative = false, bool capture_gait = false);
        auto snapshot() const -> std::string;
#endif

      private:
        struct Impl;
        bool initialize();
        Player player;
        Change change;
        std::unique_ptr<Impl> impl;
        Kh02ContextRefresh initialization;
    };
} // namespace kh3ap
