#pragma once

#include "kh3ap/features/grants/events/event_snapshot_helpers.h"
#include "kh3ap/diagnostics/ability_probe/manual_ability_state_probe_helpers.h"
#include "kh3ap/features/treasure/treasure_runtime_helpers.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeControllerAbilityService;
    class RuntimeItemGrantService;
    class RuntimeMapActorLifecycleService;
    class RuntimeMoogleShopService;
    class RuntimeRemindCompletionService;
    class RuntimeState;
    struct RuntimeApMessageGateState;

    struct RuntimePeriodicBridgeConfig
    {
        RuntimeState& runtime_state;
        RuntimeRemindCompletionService& remind_completion;
        RuntimeMoogleShopService& moogle_shop;
        RuntimeMapActorLifecycleService& map_actor_lifecycle;
        RuntimeApMessageGateState& ap_message_gate_state;
        RuntimeItemGrantService& item_grants;
        RuntimeControllerAbilityService& controller_abilities;
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        ManualAbilityStateProbeEmitter ability_array_probe{};
        ManualAbilityStateProbeEmitter targeted_sora_state_probe{};
        ManualAbilityStateProbeEmitter live_ability_owner_probe{};
        std::function<TreasureRuntimeDeps()> treasure_deps{};
        std::function<RuntimeEventSnapshotDeps()> event_snapshot_deps{};
        std::function<void()> chest_completed{};
    };

    class RuntimePeriodicBridgeService
    {
    public:
        using PhaseMarker = std::function<void(std::wstring_view)>;

        explicit RuntimePeriodicBridgeService(RuntimePeriodicBridgeConfig config);
        ~RuntimePeriodicBridgeService();

        RuntimePeriodicBridgeService(const RuntimePeriodicBridgeService&) = delete;
        auto operator=(const RuntimePeriodicBridgeService&)
                -> RuntimePeriodicBridgeService& = delete;
        RuntimePeriodicBridgeService(RuntimePeriodicBridgeService&&) noexcept;
        auto operator=(RuntimePeriodicBridgeService&&) noexcept
                -> RuntimePeriodicBridgeService&;

        auto process(
                std::chrono::steady_clock::time_point now,
                bool runtime_file_polling_ready,
                const PhaseMarker& mark_phase) -> void;

        auto request_keyblade_style_refresh() -> void;
        auto request_lifecycle_refresh() -> void;
        auto notify_treasure_changed() -> void;
        auto install_event_hooks() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
