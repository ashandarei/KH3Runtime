#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace kh3ap
{
    struct RuntimeGameThreadUpdateDecision
    {
        bool should_run{};
        bool log_first_update{};
        uint32_t game_thread_id{};
    };

    struct RuntimeGameThreadDriverSnapshot
    {
        bool unreal_initialized{};
        uint32_t game_thread_id{};
        bool update_in_progress{};
        bool first_update_logged{};
        std::chrono::steady_clock::time_point unreal_init_time{};
        std::chrono::steady_clock::time_point last_update{};
    };

    class RuntimeGameThreadDriverState
    {
    public:
        auto begin_unreal_init(
                std::chrono::steady_clock::time_point now) noexcept -> void;
        auto complete_unreal_init() noexcept -> void;
        auto mark_unreal_uninitialized() noexcept -> void;

        auto note_game_thread(uint32_t current_thread_id) noexcept -> bool;
        [[nodiscard]] auto try_begin_update(
                uint32_t current_thread_id,
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) noexcept
                -> RuntimeGameThreadUpdateDecision;
        auto finish_update() noexcept -> void;

        [[nodiscard]] auto unreal_initialized() const noexcept -> bool;
        [[nodiscard]] auto unreal_init_time() const noexcept
                -> std::chrono::steady_clock::time_point;
        [[nodiscard]] auto snapshot() const noexcept
                -> RuntimeGameThreadDriverSnapshot;

    private:
        std::atomic_bool unreal_initialized_{false};
        std::atomic<uint32_t> game_thread_id_{0};
        bool update_in_progress_{};
        bool first_update_logged_{};
        std::chrono::steady_clock::time_point unreal_init_time_{};
        std::chrono::steady_clock::time_point last_update_{};
    };
}
