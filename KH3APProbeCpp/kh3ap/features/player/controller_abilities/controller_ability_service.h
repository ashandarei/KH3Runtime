#pragma once

#include "kh3ap/features/player/keyblades/style_change_mode.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    // Presentation metadata only; does not query or mutate ownership.
    auto runtime_controller_unlock_ability_token(std::wstring_view runtime_token) -> std::wstring_view;

    struct RuntimeControllerAbilityGrantResult
    {
        std::wstring ability_token{};
        uint8_t ability_value{};
        bool newly_granted{};
        bool equipped{};
    };

    struct RuntimeControllerAbilityConfig
    {
        RuntimeState& runtime_state;
        std::wstring_view wall_run_setter_path;
        std::wstring_view style_change_setter_path;
        std::wstring_view magic_setter_path;
        std::wstring_view attraction_flow_setter_path;
        std::wstring_view airstep_setter_path;
        std::wstring_view shoot_flow_setter_path;
        std::function<RuntimeStyleChangeMode()> style_change_mode;
        bool debug_logging{};
    };

    enum class RuntimeControllerAbilityEvent : uint32_t
    {
        MenuClosed = 1,
        MapReady = 4,
        GameAbilityChanged = 8,
        Grant = 16,
        Startup = 32,
        ControllerChanged = 64,
        PawnChanged = 128,
    };

    class RuntimeControllerAbilityService
    {
    public:
        explicit RuntimeControllerAbilityService(RuntimeControllerAbilityConfig config);
        ~RuntimeControllerAbilityService();

        RuntimeControllerAbilityService(const RuntimeControllerAbilityService&) = delete;
        auto operator=(const RuntimeControllerAbilityService&)
                -> RuntimeControllerAbilityService& = delete;
        RuntimeControllerAbilityService(RuntimeControllerAbilityService&&) noexcept;
        auto operator=(RuntimeControllerAbilityService&&) noexcept
                -> RuntimeControllerAbilityService&;

        auto apply_unlock_item(
                std::wstring_view runtime_token,
                RC::Unreal::UObject* sora_pawn,
                RuntimeControllerAbilityGrantResult* result = nullptr,
                std::wstring* failure_reason = nullptr) -> bool;

        auto try_read_unlock_owned(
                std::wstring_view unlock_key,
                bool* owned,
                std::wstring* failure_reason = nullptr) const -> bool;

        auto request_refresh(RuntimeControllerAbilityEvent event) -> void;
        auto install_event_hooks() -> void;
        auto uninstall_event_hooks() -> void;

        // Drains event work only; idle calls do not inspect game objects.
        auto process_pending_refresh(
                std::chrono::steady_clock::time_point now) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
