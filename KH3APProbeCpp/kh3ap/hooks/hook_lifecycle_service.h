#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class Kh3AutosaveProbe;
    class Kh3ExpandedSaveSlotsPatch;
    class Kh3SignedMunnyPatch;
    class Kh3SqexTranslucentSliceViewGuardPatch;
    class RuntimeBlockingUiHookService;
    class RuntimeControllerAbilityService;
    class RuntimeGameThreadDriverService;
    class RuntimeNativeHookService;
    class RuntimeMinimapContextService;
    class RuntimeObjectObservationService;
    class RuntimeRetryProbeService;
    class RuntimeSoraStateHookService;

    struct RuntimeNativeHookLifecycleConfig
    {
        RuntimeNativeHookService& native_hooks;
        RuntimeObjectObservationService& object_observations;
        RuntimeGameThreadDriverService& game_thread_driver;
        RuntimeRetryProbeService& retry_probes;
        Kh3ExpandedSaveSlotsPatch& expanded_save_slots_patch;
        Kh3SignedMunnyPatch& signed_munny_patch;
        Kh3SqexTranslucentSliceViewGuardPatch&
                sqex_translucent_slice_view_guard_patch;
        Kh3AutosaveProbe& autosave_probe;
        RuntimeBlockingUiHookService& blocking_ui_hooks;
        RuntimeControllerAbilityService& controller_abilities;
        RuntimeSoraStateHookService& sora_state_hooks;
        RuntimeMinimapContextService& minimap_context;
        bool hook_install_enabled{};
        bool expanded_save_slots_patch_enabled{};
        bool signed_munny_patch_enabled{};
        bool sqex_translucent_slice_view_guard_patch_enabled{};
        bool game_engine_tick_hook_enabled{};
        bool runtime_object_observer_enabled{};
        bool autosave_probe_enabled{};
        bool steam_save_startup_guard_enabled{};
        std::wstring_view build_stamp{};
        std::wstring_view protocol_name{};
        int64_t protocol_version{};
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<void(int32_t)> manual_save_completed_handler{};
        std::function<void(const std::filesystem::path&)>
                olympus_coliseum_save_completed_handler{};
        std::function<void(std::function<void()>)>
                profile_native_dispatch_install{};
    };

    // Owns the complete production native-hook lifecycle: registry wiring,
    // object/tick observers, startup-only patches, and reverse teardown.
    class RuntimeNativeHookLifecycleService
    {
    public:
        explicit RuntimeNativeHookLifecycleService(
                RuntimeNativeHookLifecycleConfig config);
        ~RuntimeNativeHookLifecycleService();

        RuntimeNativeHookLifecycleService(
                const RuntimeNativeHookLifecycleService&) = delete;
        auto operator=(const RuntimeNativeHookLifecycleService&)
                -> RuntimeNativeHookLifecycleService& = delete;
        RuntimeNativeHookLifecycleService(
                RuntimeNativeHookLifecycleService&&) noexcept;
        auto operator=(RuntimeNativeHookLifecycleService&&) noexcept
                -> RuntimeNativeHookLifecycleService&;

        auto install() -> void;
        auto shutdown(std::wstring_view reason) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
