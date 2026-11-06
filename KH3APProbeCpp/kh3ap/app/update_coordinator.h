#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

namespace kh3ap
{
    class Kh3AutosaveProbe;
    class Kh3MemoryHooks;
    class RuntimeBattleLevelSyncService;
    class RuntimePeriodicBridgeService;
    class RuntimeQualityOfLifeService;
    class RuntimeShootFlowRestoreService;
    class RuntimeSoraStateHookService;
    class RuntimeStoryProgressService;
    class RuntimeState;
    class RuntimeWorldBootstrapService;
    struct RuntimeSoraLevelMonitorState;

    struct RuntimeUpdateCoordinatorConfig
    {
        RuntimeState& runtime_state;
        RuntimeShootFlowRestoreService& shoot_flow_restore;
        RuntimeSoraLevelMonitorState& sora_monitor;
        RuntimeBattleLevelSyncService& battle_level_sync;
        Kh3MemoryHooks& memory_hooks;
        RuntimeStoryProgressService& story_progress;
        RuntimeQualityOfLifeService& quality_of_life;
        RuntimeWorldBootstrapService& world_bootstrap;
        Kh3AutosaveProbe& autosave_probe;
        RuntimeSoraStateHookService& sora_state_hooks;
        RuntimePeriodicBridgeService& periodic_bridge;
        std::function<void(bool)> drain_runtime_object_observations{};
        std::function<void(std::chrono::steady_clock::time_point)>
                process_lucky_emblem_tracking{};
        std::function<void()> refresh_retry_blueprint_targets{};
        std::function<void()> flush_retry_blueprint_events{};
        std::function<void()> flush_retry_point_events{};
        std::function<void()> process_pending_goa_autosave{};
        std::function<void(std::chrono::steady_clock::time_point)>
                process_controller_reacquire{};
        std::function<void()> process_union_cross_completion{};
        std::function<void()> process_beat_of_lead_completion{};
    };

    class RuntimeUpdateCoordinator
    {
    public:
        using PhaseMarker = std::function<void(std::wstring_view)>;

        explicit RuntimeUpdateCoordinator(RuntimeUpdateCoordinatorConfig config);
        ~RuntimeUpdateCoordinator();

        RuntimeUpdateCoordinator(const RuntimeUpdateCoordinator&) = delete;
        auto operator=(const RuntimeUpdateCoordinator&)
                -> RuntimeUpdateCoordinator& = delete;
        RuntimeUpdateCoordinator(RuntimeUpdateCoordinator&&) noexcept;
        auto operator=(RuntimeUpdateCoordinator&&) noexcept
                -> RuntimeUpdateCoordinator&;

        auto tick(
                bool unreal_initialized,
                bool title_or_ui_controller_active,
                bool ignored_map_active,
                std::chrono::steady_clock::time_point unreal_init_time,
                const PhaseMarker& mark_phase) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
