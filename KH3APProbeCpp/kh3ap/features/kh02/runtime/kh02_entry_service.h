#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Resident entry layer: portal interaction and pre-deserialization save gate.
    // It constructs no story/presentation state until preparation is requested.
    class Kh02EntryService
    {
      public:
        using Prepare = std::function<bool(RC::Unreal::UObject*)>;
        explicit Kh02EntryService(Prepare prepare, std::function<bool()> assets_notified = {},
                                  std::function<void()> prepare_asset_access = {});
        ~Kh02EntryService();
        static auto prepare_startup(RC::Unreal::UObject* context) -> bool;
        static auto prepare_journal_assets() -> bool;
        void initialize();
        void tick_bootstrap();
        void tick_portal(RC::Unreal::UObject* pawn, bool playable);
        void leave_portal();
        auto failed_preparation() const -> bool;
        auto portal_work_pending() const -> bool;
        void require_restart(const std::string& reason);
        auto bootstrap_snapshot() const -> std::string;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto portal_command(RC::Unreal::UObject* pawn, std::wstring_view action) -> std::string;
#endif
      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
