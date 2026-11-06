#pragma once

#include <chrono>
#include <cstdint>
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
    class RuntimeGoaSettingsService;
    class RuntimeState;
    struct RuntimeSoraLevelMonitorState;

    struct RuntimeBattleLevelSyncStatus
    {
        bool enabled{true};
        int32_t offset{};
    };

    struct RuntimeBattleLevelSyncConfig
    {
        RuntimeState& runtime_state;
        RuntimeSoraLevelMonitorState& sora_level_monitor;
        RuntimeGoaSettingsService& goa_settings;
        std::function<bool(
                RC::Unreal::UObject*,
                std::wstring_view,
                std::wstring_view,
                bool)> cache_pawn_from_controller{};
        std::function<void(std::wstring_view, uint64_t)> profile_sample_recorder{};
    };

    class RuntimeBattleLevelSyncService
    {
    public:
        explicit RuntimeBattleLevelSyncService(RuntimeBattleLevelSyncConfig config);
        ~RuntimeBattleLevelSyncService();

        RuntimeBattleLevelSyncService(const RuntimeBattleLevelSyncService&) = delete;
        auto operator=(const RuntimeBattleLevelSyncService&)
                -> RuntimeBattleLevelSyncService& = delete;
        RuntimeBattleLevelSyncService(RuntimeBattleLevelSyncService&&) noexcept;
        auto operator=(RuntimeBattleLevelSyncService&&) noexcept
                -> RuntimeBattleLevelSyncService&;

        auto reset_for_map_lifecycle() -> void;
        [[nodiscard]] auto status() const -> RuntimeBattleLevelSyncStatus;
        auto set_enabled(bool enabled) -> bool;
        auto set_offset(int32_t offset) -> bool;
        auto process_pending(std::chrono::steady_clock::time_point now) -> void;
        auto schedule_initial(uint64_t level, RC::Unreal::UObject* validated_sora_pawn) -> void;
        auto schedule_he06_special(
                std::wstring_view frame_name,
                std::chrono::steady_clock::time_point now) -> bool;
        auto sync_observed(std::wstring_view source, bool force = false) -> bool;
        auto sync_sora_level(
                uint64_t sora_level,
                std::wstring_view source,
                RC::Unreal::UObject* sora_pawn,
                bool force = false,
                bool sora_pawn_already_classified = false,
                bool bypass_cinematic_gate = false) -> bool;
        auto sync_after_controller_reacquire(std::wstring_view source) -> bool;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
