#pragma once

#include "kh3ap/core/performance_build.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace kh3ap
{
    struct RuntimeGameThreadDriverConfig
    {
        bool tick_hook_enabled{};
        bool update_loop_enabled{};
        std::wstring_view build_stamp{};
        std::chrono::steady_clock::duration update_interval{};
        std::function<void(
                bool,
                std::chrono::steady_clock::time_point)> run_update{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        std::function<std::wstring()> hitch_context{};
#endif
    };

    // Owns UGameEngine::Tick hook lifecycle and the bounded game-thread update
    // gate. RuntimeUpdateCoordinator remains responsible for the update body.
    class RuntimeGameThreadDriverService
    {
    public:
        explicit RuntimeGameThreadDriverService(
                RuntimeGameThreadDriverConfig config);
        ~RuntimeGameThreadDriverService();

        RuntimeGameThreadDriverService(
                const RuntimeGameThreadDriverService&) = delete;
        auto operator=(const RuntimeGameThreadDriverService&)
                -> RuntimeGameThreadDriverService& = delete;
        RuntimeGameThreadDriverService(
                RuntimeGameThreadDriverService&&) noexcept;
        auto operator=(RuntimeGameThreadDriverService&&) noexcept
                -> RuntimeGameThreadDriverService&;

        auto begin_unreal_init() -> void;
        auto complete_unreal_init() -> void;
        auto mark_unreal_uninitialized() -> void;
        auto note_game_thread(std::wstring_view source) -> void;

        auto install_tick_hook() -> bool;
        auto uninstall_tick_hook() -> void;

        [[nodiscard]] auto unreal_init_time() const
                -> std::chrono::steady_clock::time_point;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
