#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "kh3ap/features/kh02/combat/kh02_battle_level_policy.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct Kh02RuntimeConfig
    {
        std::function<RC::Unreal::UObject*()> player;
        std::function<bool()> gameplay_ready;
        std::function<bool()> game_over;
        std::function<void(const std::string&)> report_error;
        std::function<void()> shared_unlocks_ready;
        std::function<Kh02BattleLevelSettings()> battle_level_settings;
    };

    // Application-owned lifetime. Test request polling is a separate client of
    // this service and does not drive the story or presentation clocks.
    class Kh02Runtime
    {
      public:
        explicit Kh02Runtime(Kh02RuntimeConfig config);
        ~Kh02Runtime();
        void initialize();
        void tick(bool unreal_initialized);
        auto error() const -> const std::string&;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto story_command(RC::Unreal::UObject* pawn, std::wstring_view action) -> std::optional<std::string>;
        auto portal_command(RC::Unreal::UObject* pawn, std::wstring_view action) -> std::string;
#endif

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
