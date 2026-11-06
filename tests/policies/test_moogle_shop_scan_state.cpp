#include "kh3ap/features/moogle_shop/moogle_shop_scan_state.h"
#include "kh3ap/bridge/serialization/json_fields.h"

#include <array>
#include <cassert>
#include <cstddef>

int main()
{
    const std::string slot_options = R"({
        "moogle_shop_checks": 0,
        "negative": -7,
        "overflow": 999999999999999999999999999999
    })";
    assert(kh3ap::JsonFieldReader::int64_field(slot_options, "moogle_shop_checks") == 0);
    assert(kh3ap::JsonFieldReader::int64_field(slot_options, "negative") == -7);
    assert(!kh3ap::JsonFieldReader::int64_field(slot_options, "missing").has_value());
    assert(!kh3ap::JsonFieldReader::int64_field(slot_options, "overflow").has_value());

    assert(!kh3ap::runtime_moogle_shop_checks_enabled(std::nullopt));
    assert(!kh3ap::runtime_moogle_shop_checks_enabled(0));
    assert(!kh3ap::runtime_moogle_shop_checks_enabled(-1));
    assert(kh3ap::runtime_moogle_shop_checks_enabled(1));
    assert(kh3ap::runtime_moogle_shop_checks_enabled(20));

    kh3ap::RuntimeMoogleShopActivationGate bridge_refresh_gate{};
    assert(!bridge_refresh_gate.active());
    assert(bridge_refresh_gate.activate());
    assert(bridge_refresh_gate.active());
    assert(!bridge_refresh_gate.activate());
    bridge_refresh_gate.reset(false);
    assert(!bridge_refresh_gate.active());
    bridge_refresh_gate.reset(true);
    assert(bridge_refresh_gate.active());

    kh3ap::RuntimeMoogleShopDeliveryState delivery{};
    assert(!delivery.report_in_flight());
    assert(delivery.mark_report_in_flight(L"MoogleShop:AP_ITEM_03"));
    assert(delivery.report_in_flight());
    assert(delivery.pending_bridge_key() == L"MoogleShop:AP_ITEM_03");
    assert(!delivery.mark_report_in_flight(L"MoogleShop:AP_ITEM_05"));
    assert(!delivery.acknowledge(L"MoogleShop:AP_ITEM_05"));
    assert(delivery.report_in_flight());
    assert(delivery.acknowledge(L"MoogleShop:AP_ITEM_03"));
    assert(!delivery.report_in_flight());
    assert(delivery.mark_report_in_flight(L"MoogleShop:AP_ITEM_05"));
    delivery.clear();
    assert(!delivery.report_in_flight());

    kh3ap::RuntimeMoogleShopScanState state{};

    assert(state.purchase_scan_requested());
    assert(!state.purchase_scan_active());
    assert(state.reconciliation_requested());
    assert(!state.reconciliation_active());
    assert(state.ultima_refresh_requested());
    state.complete_ultima_refresh();
    assert(!state.ultima_refresh_requested());
    assert(state.request_ultima_refresh());
    assert(!state.request_ultima_refresh());
    assert(state.begin_reconciliation(5));
    assert(state.reconciliation_active());

    const std::array<bool, 5> checked{false, true, false, true, false};
    const auto checked_at = [&](std::size_t index) { return checked[index]; };
    assert(state.next_reconciliation_index(checked.size(), checked_at) == 1);
    assert(state.next_reconciliation_index(checked.size(), checked_at) == 3);
    assert(!state.next_reconciliation_index(checked.size(), checked_at).has_value());
    assert(!state.reconciliation_active());
    assert(!state.reconciliation_requested());

    assert(state.request_reconciliation());
    assert(state.begin_reconciliation(checked.size()));
    assert(state.next_reconciliation_index(checked.size(), checked_at) == 1);
    assert(state.request_reconciliation());
    assert(!state.begin_reconciliation(checked.size()));
    assert(state.next_reconciliation_index(checked.size(), checked_at) == 3);
    assert(!state.next_reconciliation_index(checked.size(), checked_at).has_value());
    assert(state.begin_reconciliation(checked.size()));

    state.cancel_reconciliation_for_retry();
    assert(!state.reconciliation_active());
    assert(state.reconciliation_requested());

    state.reset();
    const std::array<bool, 5> purchase_eligible{true, false, true, false, true};
    const auto eligible_at = [&](std::size_t index) { return purchase_eligible[index]; };
    assert(state.set_shop_session_active(true));
    assert(state.shop_session_active());
    assert(state.begin_purchase_scan(purchase_eligible.size()));
    assert(state.purchase_scan_active());
    assert(state.next_purchase_index(purchase_eligible.size(), eligible_at) == 0);
    assert(state.next_purchase_index(purchase_eligible.size(), eligible_at) == 2);
    assert(state.next_purchase_index(purchase_eligible.size(), eligible_at) == 4);
    assert(!state.next_purchase_index(purchase_eligible.size(), eligible_at).has_value());
    assert(!state.purchase_scan_active());
    assert(!state.purchase_scan_requested());

    assert(state.request_purchase_scan());
    assert(!state.request_purchase_scan());
    assert(state.begin_purchase_scan(purchase_eligible.size()));
    assert(state.next_purchase_index(purchase_eligible.size(), eligible_at) == 0);
    state.cancel_purchase_scan_for_retry();
    assert(!state.purchase_scan_active());
    assert(state.purchase_scan_requested());

    assert(state.set_shop_session_active(false));
    assert(!state.shop_session_active());
    assert(!state.set_shop_session_active(false));
    assert(state.set_shop_session_active(true));
    assert(state.begin_purchase_scan(purchase_eligible.size()));
    assert(state.next_purchase_index(purchase_eligible.size(), eligible_at) == 0);

    const auto never_eligible = [](std::size_t) { return false; };
    state.cancel_purchase_scan_for_retry();
    assert(state.begin_purchase_scan(5));
    assert(!state.next_purchase_index(5, never_eligible).has_value());
    assert(state.request_purchase_scan());
    assert(!state.begin_purchase_scan(0));
    assert(!state.next_purchase_index(0, eligible_at).has_value());

    return 0;
}
