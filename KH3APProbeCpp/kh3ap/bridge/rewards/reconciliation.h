#pragma once
#include "kh3ap/bridge/rewards/receipts.h"
#include "kh3ap/bridge/checks/chest_snapshot.h"
#include "kh3ap/features/world/portals/gate_opener_policy.h"
#include "kh3ap/features/kh02/progress/kh02_progress_state.h"
#include "kh3ap/features/grants/delivery/item_delivery_catalog.h"
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace kh3ap::reconciliation
{
    inline constexpr auto journal_file = L"runtime_native_reconciliation.json";
    struct View
    {
        bool ready{};
        int64_t epoch{};
        Receipts active;
        std::string error;
        std::string deathlink_request;
        uint64_t delivery_revision{};
        std::shared_ptr<const item_delivery::ExpandedCatalog> expanded;
        std::shared_ptr<const RuntimeItemGrantBatchRequest> delivery;
        std::wstring seed;
        gate_opener::State gates;
        std::set<int64_t> gate_receipts;
        int64_t slot{};
        std::shared_ptr<const RuntimeApMessageRequest> message;
        std::set<std::string> completed_checks;
        bool goal_complete{};
        bool adversary_checks{};
        std::shared_ptr<const chest_manifest::Snapshot> chests;
        bool kh02_progress_ready{};
        uint64_t kh02_progress_revision{};
        int64_t loaded_save_slot{-1};
        kh02_progress::SnapshotPtr kh02_progress;
        std::string kh02_progress_error;
        std::set<std::wstring> kh02_ap_chests;
    };
    // Worker owns JSON decisions and durable randomizer metadata. Producers
    // pass values only; there is no timer, save-file inspection, or UObject.
    auto start(const std::filesystem::path& root) -> void;
    auto stop() -> void;
    auto view() -> std::shared_ptr<const View>;
    auto epoch() -> int64_t;
    auto open_gate(int portal, const View& expected) -> void;
    auto observe(std::string_view channel, std::string_view json) -> void;
    auto auto_save_begin(uint64_t filetime) -> void;
    auto kh02_progress_update(kh02_progress::Snapshot progress,uint64_t restore_revision,
        std::wstring_view seed,int64_t player_slot) -> void;
    auto kh02_progress_reset(uint64_t restore_revision, std::wstring_view seed, int64_t player_slot) -> void;
    auto inventory_snapshot_observer(bool enabled) -> void;
    auto inventory_snapshot(bool restored, uint64_t filetime, bool outside_item_grant = true,
        std::array<uint8_t,32> bonuses = {}) -> void;
    auto file_open(std::wstring_view path, uint32_t access, uint64_t event, uint64_t filetime, bool succeeded = true) -> void;
    auto file_closed(uint64_t event, bool succeeded) -> void;
    auto deathlink_begin(std::string_view request_id) -> void;
    auto deathlink_end(std::string_view request_id) -> void;
    auto persistent_reward_notice(const RuntimeItemGrantRequest& request, int64_t delivery_epoch) -> void;
    auto message_completed(std::wstring_view request_id, int64_t delivery_epoch) -> void;
}
