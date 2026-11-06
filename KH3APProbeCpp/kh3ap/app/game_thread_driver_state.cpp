#include "kh3ap/app/game_thread_driver_state.h"

#include "kh3ap/bridge/catalog/update_catalog_state.h"

namespace kh3ap
{
    auto RuntimeGameThreadDriverState::begin_unreal_init(
            std::chrono::steady_clock::time_point now) noexcept -> void
    {
        unreal_init_time_ = now;
    }

    auto RuntimeGameThreadDriverState::complete_unreal_init() noexcept -> void
    {
        unreal_initialized_.store(true, std::memory_order_release);
    }

    auto RuntimeGameThreadDriverState::mark_unreal_uninitialized() noexcept -> void
    {
        unreal_initialized_.store(false, std::memory_order_release);
    }

    auto RuntimeGameThreadDriverState::note_game_thread(
            uint32_t current_thread_id) noexcept -> bool
    {
        uint32_t expected_thread_id = 0;
        return game_thread_id_.compare_exchange_strong(
                expected_thread_id,
                current_thread_id,
                std::memory_order_acq_rel);
    }

    auto RuntimeGameThreadDriverState::try_begin_update(
            uint32_t current_thread_id,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) noexcept
            -> RuntimeGameThreadUpdateDecision
    {
        const uint32_t game_thread_id =
                game_thread_id_.load(std::memory_order_acquire);
        if (game_thread_id == 0 ||
            current_thread_id != game_thread_id ||
            !unreal_initialized_.load(std::memory_order_acquire) ||
            update_in_progress_ ||
            !should_run_periodic_update(last_update_, now, interval))
        {
            return {};
        }

        update_in_progress_ = true;
        const bool log_first_update = !first_update_logged_;
        first_update_logged_ = true;
        return {
                .should_run = true,
                .log_first_update = log_first_update,
                .game_thread_id = game_thread_id,
        };
    }

    auto RuntimeGameThreadDriverState::finish_update() noexcept -> void
    {
        update_in_progress_ = false;
    }

    auto RuntimeGameThreadDriverState::unreal_initialized() const noexcept -> bool
    {
        return unreal_initialized_.load(std::memory_order_acquire);
    }

    auto RuntimeGameThreadDriverState::unreal_init_time() const noexcept
            -> std::chrono::steady_clock::time_point
    {
        return unreal_init_time_;
    }

    auto RuntimeGameThreadDriverState::snapshot() const noexcept
            -> RuntimeGameThreadDriverSnapshot
    {
        return {
                .unreal_initialized = unreal_initialized(),
                .game_thread_id = game_thread_id_.load(std::memory_order_acquire),
                .update_in_progress = update_in_progress_,
                .first_update_logged = first_update_logged_,
                .unreal_init_time = unreal_init_time_,
                .last_update = last_update_,
        };
    }
}
