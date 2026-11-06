#pragma once

#include "kh3ap/core/types.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;
    struct RuntimeMoogleShopConfig
    {
        RuntimeState& runtime_state;
        std::function<bool(const RuntimeLootPayload&)> payload_emitter{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::wstring_view have_item_path{};
        std::wstring_view get_item_path{};
        std::wstring_view add_munny_path{};
        std::wstring_view blueprint_library_default_object_path{};
        std::chrono::steady_clock::duration report_retry_interval{};
        std::chrono::steady_clock::duration inventory_query_retry_interval{};
        std::size_t max_catalog_locations{20};
        bool enabled{true};
        bool debug_logging{false};
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::function<std::chrono::steady_clock::time_point()> unreal_init_time_resolver{};
        std::wstring_view build_stamp{};
        int material_item_index_min{61};
        int material_item_index_max{80};
        bool material_enum_augmentation_enabled{false};
    };

    class RuntimeMoogleShopService
    {
    public:
        explicit RuntimeMoogleShopService(RuntimeMoogleShopConfig config);
        ~RuntimeMoogleShopService();

        RuntimeMoogleShopService(const RuntimeMoogleShopService&) = delete;
        auto operator=(const RuntimeMoogleShopService&) -> RuntimeMoogleShopService& = delete;
        RuntimeMoogleShopService(RuntimeMoogleShopService&&) noexcept;
        auto operator=(RuntimeMoogleShopService&&) noexcept -> RuntimeMoogleShopService&;

        auto initialize_runtime() -> void;
        auto observe_shop_session(bool active) -> void;
        auto observe_progressive_keyblade_level_applied() -> void;
        auto observe_player_started(std::wstring_view source) -> void;
        // Queue reconciliation for the next observed ShopMenu session.
        auto request_reconciliation(std::wstring_view source) -> void;
        auto reset() -> void;
        auto refresh(const std::optional<std::filesystem::path>& bridge_dir) -> void;
        auto has_pending_work() const -> bool;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
