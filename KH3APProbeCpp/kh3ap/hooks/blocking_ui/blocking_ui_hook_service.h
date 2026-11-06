#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;
    struct RuntimeApMessageGateState;

    struct RuntimeBlockingUiHookConfig
    {
        bool enabled{};
        bool shoot_flow_restore_enabled{};
        RuntimeState& runtime_state;
        RuntimeApMessageGateState& ap_message_gate_state;
        std::function<bool()> ignored_map_active{};
        std::function<bool()> gameplay_controller_active{};
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<void(bool)> observe_pause_ui{};
        std::wstring_view build_stamp{};
        std::chrono::milliseconds quiet_window{};
        std::chrono::milliseconds focus_handoff_grace{};
        std::function<void()> on_menu_closed{};
        std::function<void()> before_map_load{};
    };

    // Owns the pause transition observer. Registration happens from the Unreal
    // initialization phase and uses UE4SS's global script and ProcessEvent
    // detours. The hot paths use a fixed raw-FName lifecycle catalog and do no
    // allocation before a match. A one-shot internal deadline is checked only
    // after the last focus owner exits; it never scans objects or polls game
    // state.
    class RuntimeBlockingUiHookService
    {
    public:
        explicit RuntimeBlockingUiHookService(RuntimeBlockingUiHookConfig config);
        ~RuntimeBlockingUiHookService();

        RuntimeBlockingUiHookService(
                const RuntimeBlockingUiHookService&) = delete;
        auto operator=(const RuntimeBlockingUiHookService&)
                -> RuntimeBlockingUiHookService& = delete;
        RuntimeBlockingUiHookService(RuntimeBlockingUiHookService&&) noexcept;
        auto operator=(RuntimeBlockingUiHookService&&) noexcept
                -> RuntimeBlockingUiHookService&;

        auto install_once() -> void;
        auto process_pending_focus_release(
                std::chrono::steady_clock::time_point now) -> void;
        auto uninstall(std::wstring_view reason) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
