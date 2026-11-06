#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap
{
    [[nodiscard]] constexpr auto runtime_moogle_shop_checks_enabled(
            const std::optional<int64_t> check_count) -> bool
    {
        return check_count.has_value() && *check_count > 0;
    }

    class RuntimeMoogleShopActivationGate
    {
    public:
        auto activate() -> bool
        {
            const bool newly_activated = !active_;
            active_ = true;
            return newly_activated;
        }

        auto reset(bool active) -> void
        {
            active_ = active;
        }

        [[nodiscard]] auto active() const -> bool
        {
            return active_;
        }

    private:
        bool active_{false};
    };

    class RuntimeMoogleShopDeliveryState
    {
    public:
        auto mark_report_in_flight(std::wstring bridge_key) -> bool
        {
            if (report_in_flight() || bridge_key.empty())
            {
                return false;
            }
            pending_bridge_key_ = std::move(bridge_key);
            return true;
        }

        auto acknowledge(std::wstring_view bridge_key) -> bool
        {
            if (bridge_key != pending_bridge_key_)
            {
                return false;
            }
            pending_bridge_key_.clear();
            return true;
        }

        auto clear() -> void
        {
            pending_bridge_key_.clear();
        }

        [[nodiscard]] auto report_in_flight() const -> bool
        {
            return !pending_bridge_key_.empty();
        }

        [[nodiscard]] auto pending_bridge_key() const -> std::wstring_view
        {
            return pending_bridge_key_;
        }

    private:
        std::wstring pending_bridge_key_{};
    };

    class RuntimeMoogleShopScanState
    {
    public:
        auto reset() -> void
        {
            purchase_index_ = 0;
            purchase_remaining_ = 0;
            reconcile_index_ = 0;
            reconcile_remaining_ = 0;
            purchase_scan_requested_ = true;
            reconciliation_requested_ = true;
            ultima_refresh_requested_ = true;
            shop_session_active_ = false;
        }

        auto set_shop_session_active(bool active) -> bool
        {
            if (active == shop_session_active_)
            {
                return false;
            }

            shop_session_active_ = active;
            purchase_index_ = 0;
            return true;
        }

        [[nodiscard]] auto shop_session_active() const -> bool
        {
            return shop_session_active_;
        }

        auto request_purchase_scan() -> bool
        {
            const bool newly_requested = !purchase_scan_requested_;
            purchase_scan_requested_ = true;
            return newly_requested;
        }

        [[nodiscard]] auto purchase_scan_requested() const -> bool
        {
            return purchase_scan_requested_;
        }

        [[nodiscard]] auto purchase_scan_active() const -> bool
        {
            return purchase_remaining_ != 0;
        }

        auto begin_purchase_scan(std::size_t location_count) -> bool
        {
            if (!purchase_scan_requested_ || purchase_scan_active())
            {
                return false;
            }

            purchase_scan_requested_ = false;
            purchase_index_ = 0;
            purchase_remaining_ = location_count;
            return purchase_scan_active();
        }

        auto cancel_purchase_scan_for_retry() -> void
        {
            purchase_remaining_ = 0;
            purchase_scan_requested_ = true;
        }

        auto request_ultima_refresh() -> bool
        {
            const bool newly_requested = !ultima_refresh_requested_;
            ultima_refresh_requested_ = true;
            return newly_requested;
        }

        [[nodiscard]] auto ultima_refresh_requested() const -> bool
        {
            return ultima_refresh_requested_;
        }

        auto complete_ultima_refresh() -> void
        {
            ultima_refresh_requested_ = false;
        }

        auto request_reconciliation() -> bool
        {
            const bool newly_requested = !reconciliation_requested_;
            reconciliation_requested_ = true;
            return newly_requested;
        }

        [[nodiscard]] auto reconciliation_requested() const -> bool
        {
            return reconciliation_requested_;
        }

        [[nodiscard]] auto reconciliation_active() const -> bool
        {
            return reconcile_remaining_ != 0;
        }

        auto begin_reconciliation(std::size_t location_count) -> bool
        {
            if (!reconciliation_requested_ || reconciliation_active())
            {
                return false;
            }

            reconciliation_requested_ = false;
            reconcile_index_ = 0;
            reconcile_remaining_ = location_count;
            return reconciliation_active();
        }

        auto cancel_reconciliation_for_retry() -> void
        {
            reconcile_remaining_ = 0;
            reconciliation_requested_ = true;
        }

        template <typename EligiblePredicate>
        auto next_reconciliation_index(
                std::size_t location_count,
                EligiblePredicate&& eligible) -> std::optional<std::size_t>
        {
            if (location_count == 0)
            {
                reconcile_remaining_ = 0;
                return std::nullopt;
            }

            while (reconcile_remaining_ != 0)
            {
                const std::size_t index = reconcile_index_ % location_count;
                reconcile_index_ = (index + 1) % location_count;
                --reconcile_remaining_;
                if (eligible(index))
                {
                    return index;
                }
            }

            return std::nullopt;
        }

        template <typename EligiblePredicate>
        auto next_purchase_index(
                std::size_t location_count,
                EligiblePredicate&& eligible) -> std::optional<std::size_t>
        {
            if (location_count == 0)
            {
                purchase_remaining_ = 0;
                return std::nullopt;
            }

            while (purchase_remaining_ != 0)
            {
                const std::size_t index = purchase_index_ % location_count;
                purchase_index_ = (index + 1) % location_count;
                --purchase_remaining_;
                if (eligible(index))
                {
                    return index;
                }
            }

            return std::nullopt;
        }

    private:
        std::size_t purchase_index_{0};
        std::size_t purchase_remaining_{0};
        std::size_t reconcile_index_{0};
        std::size_t reconcile_remaining_{0};
        bool purchase_scan_requested_{true};
        bool reconciliation_requested_{true};
        bool ultima_refresh_requested_{true};
        bool shop_session_active_{false};
    };
}
