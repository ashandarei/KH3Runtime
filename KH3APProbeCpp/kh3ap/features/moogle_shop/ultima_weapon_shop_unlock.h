#pragma once

#include "ultima_weapon_shop_helpers.h"

#include <chrono>
#include <string_view>

namespace kh3ap
{
    class RuntimeMoogleShopScanState;
    struct UltimaWeaponShopOptions;

    class UltimaWeaponShopUnlock
    {
    public:
        explicit UltimaWeaponShopUnlock(RuntimeMoogleShopScanState& scan_state, bool debug_logging);

        auto request_refresh(std::wstring_view source) -> void;
        auto reset_retry_window() -> void;
        auto reset() -> void;
        auto process(std::chrono::steady_clock::time_point now, bool seed_loaded,
                     const UltimaWeaponShopOptions& options) -> void;

    private:
        auto defer_retry(std::chrono::steady_clock::time_point now) -> void;

        RuntimeMoogleShopScanState& scan_state_;
        bool debug_logging_;
        UltimaWeaponShopRowCache row_cache_{};
        int retry_attempts_{0};
        std::chrono::steady_clock::time_point retry_not_before_{};
        bool failure_logged_{false};
    };
} // namespace kh3ap
