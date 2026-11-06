#include "kh3ap/features/grants/delivery/item_delivery_binding.h"
#include "kh3ap/features/grants/delivery/item_delivery_wake.h"
#include <iostream>
#include <cstdlib>
#include <fstream>
#include "kh3ap/bridge/serialization/json.h"

namespace {
std::shared_ptr<const kh3ap::json::SeedDocument> pak_document;
auto pak_source(std::string_view name) -> std::shared_ptr<const kh3ap::json::SeedDocument>
{ return name == "slot_data.json" ? pak_document : nullptr; }
}

using kh3ap::item_delivery::delivery_binding_error;

int main()
{
    const auto check = [](bool ok) { if (!ok) std::abort(); };
    kh3ap::item_delivery::DeliveryWakeSchedule wake;
    const auto idle_at = std::chrono::steady_clock::now();
    check(!wake.should_process(idle_at, false));
    check(!wake.should_process(idle_at + std::chrono::seconds(1), false));
    wake.set_pending(true);
    const auto now = idle_at + std::chrono::seconds(2);
    check(wake.should_process(now, false));
    for (int ms = 1; ms < 1000; ++ms)
        check(!wake.should_process(now + std::chrono::milliseconds(ms), false));
    check(wake.should_process(now + std::chrono::seconds(1), false));
    check(wake.should_process(now + std::chrono::milliseconds(1001), true));
    check(!wake.should_process(now + std::chrono::milliseconds(1002), false));
    wake.request();
    wake.request(); // Coalesce repeated callbacks without delaying delivery.
    check(wake.should_process(now + std::chrono::milliseconds(1003), false));
    check(!wake.should_process(now + std::chrono::milliseconds(1004), false));
    wake.request(); // A callback after consumption cannot be lost.
    check(wake.should_process(now + std::chrono::milliseconds(1005), false));
    check(!wake.should_process(now + std::chrono::milliseconds(1006), false));
    wake.set_pending(false);
    check(!wake.should_process(now + std::chrono::seconds(3), false));
    check(wake.should_process(now + std::chrono::seconds(3), true));
    wake.request();
    check(wake.should_process(now + std::chrono::seconds(4), false));
    check(!wake.should_process(now + std::chrono::seconds(5), false));
    check(delivery_binding_error(L"kh3ap.item_delivery.v1", 42, 42, L"seed", 1, L"seed", 1, L"seed", 1).empty());
    check(delivery_binding_error(L"", 42, 42, L"seed", 1, L"seed", 1, L"seed", 1) == L"request_schema_mismatch");
    for (auto pid : {0, 41, 43})
        check(delivery_binding_error(L"kh3ap.item_delivery.v1", pid, 42, L"seed", 1, L"seed", 1, L"seed", 1) == L"request_process_mismatch");
    for (auto seed : {L"", L"wrong"})
        check(delivery_binding_error(L"kh3ap.item_delivery.v1", 42, 42, seed, 1, L"seed", 1, L"seed", 1) == L"request_seed_mismatch");
    for (auto slot : {0, 2})
        check(delivery_binding_error(L"kh3ap.item_delivery.v1", 42, 42, L"seed", slot, L"seed", 1, L"seed", 1) == L"request_seed_mismatch");
    check(delivery_binding_error(L"kh3ap.item_delivery.v1", 42, 42, L"seed", 1, L"wrong", 1, L"seed", 1) == L"request_seed_mismatch");
    check(delivery_binding_error(L"kh3ap.item_delivery.v1", 42, 42, L"seed", 1, L"seed", 2, L"seed", 1) == L"request_seed_mismatch");
    check(delivery_binding_error(L"kh3ap.item_delivery.v1", 42, 42, L"seed", 1, L"seed", 1, L"wrong", 1) == L"request_seed_mismatch");
    check(delivery_binding_error(L"kh3ap.item_delivery.v1", 42, 42, L"seed", 1, L"seed", 1, L"seed", 2) == L"request_seed_mismatch");
    // A bound game reads immutable pak documents even when a loose export is
    // absent, corrupt or belongs to a different seed. Missing pak data fails closed.
    pak_document = kh3ap::json::decode_seed_document("slot_data.json",
        R"({"kh3_seed":{"seed_name":"pak-seed","slot":7},"bridge":{"locations":[]}})");
    kh3ap::json::bind_runtime_seed_source(pak_source);
    const auto path = std::filesystem::temp_directory_path() / "kh3-pak-binding-fixture" / "slot_data.json";
    std::filesystem::create_directories(path.parent_path());
    check(kh3ap::json::read_seed_document(path) == pak_document);
    std::ofstream(path) << R"({"kh3_seed":{"seed_name":"wrong","slot":2}})";
    check(kh3ap::json::read_seed_document(path) == pak_document);
    check(kh3ap::item_delivery::installed_delivery_seed_identity().seed == L"pak-seed");
    check(kh3ap::item_delivery::installed_delivery_seed_identity().slot == 7);
    pak_document.reset();
    check(kh3ap::item_delivery::installed_delivery_seed_identity().seed.empty());
    std::filesystem::remove(path);
    std::filesystem::remove(path.parent_path());
    std::cout << "Native item delivery binding tests passed.\n";
}
