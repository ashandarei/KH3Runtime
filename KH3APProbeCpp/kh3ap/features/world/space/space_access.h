#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace kh3ap
{
    // Receipt-derived access, refreshed only on bridge input events/bootstrap.
    struct RuntimeSpaceAccess
    {
        using BridgeResolver = std::function<std::optional<std::filesystem::path>()>;
        using ConfigureRewards = std::function<void(const std::filesystem::path&, const std::wstring&, int64_t, bool)>;
        explicit RuntimeSpaceAccess(BridgeResolver resolver) : bridge_resolver(std::move(resolver)) {}
        void refresh(const ConfigureRewards& configure_rewards);
        std::set<std::wstring> items;
        std::set<std::wstring> allowed;
        std::filesystem::path bridge;
        std::wstring seed;
        bool access_ready{};
        bool include_space{};
        bool party_portals{};
        bool kh02_enabled{};
        int64_t ap_slot{};
        int heart_piece_count{}, worlds_required{};
    private:
        BridgeResolver bridge_resolver;
        std::array<std::filesystem::file_time_type, 2> input_times{};
        std::array<uint64_t, 2> input_revisions{};
        struct SlotSettings {
            std::wstring seed;
            int64_t slot{};
            int heart_piece_count{}, worlds_required{};
            bool include_space{}, party_portals{}, kh02_enabled{};
        };
        std::optional<SlotSettings> slot_settings;
    };
}
