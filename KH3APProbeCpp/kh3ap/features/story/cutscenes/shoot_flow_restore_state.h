#pragma once

#include <chrono>
#include <cstdint>

namespace kh3ap
{
    enum class RuntimeShootFlowRestoreObservation
    {
        Ignored,
        SpecialBattleArmed,
        TutorialOpened,
        TutorialReleased,
        PostTutorialCinematicSeen,
        RestoreQueued,
    };

    class RuntimeShootFlowRestoreState
    {
    public:
        auto observe_special_battle(bool active) -> RuntimeShootFlowRestoreObservation
        {
            if (!active || consumed_for_pawn_ || special_battle_seen_)
            {
                return RuntimeShootFlowRestoreObservation::Ignored;
            }

            special_battle_seen_ = true;
            ++special_battle_count_;
            return RuntimeShootFlowRestoreObservation::SpecialBattleArmed;
        }

        auto observe_pause_ui(bool active) -> RuntimeShootFlowRestoreObservation
        {
            if (!special_battle_seen_ || consumed_for_pawn_ || pending_ || tutorial_released_)
            {
                return RuntimeShootFlowRestoreObservation::Ignored;
            }

            if (active)
            {
                if (tutorial_ui_open_)
                {
                    return RuntimeShootFlowRestoreObservation::Ignored;
                }
                tutorial_ui_open_ = true;
                ++tutorial_open_count_;
                return RuntimeShootFlowRestoreObservation::TutorialOpened;
            }

            if (!tutorial_ui_open_)
            {
                return RuntimeShootFlowRestoreObservation::Ignored;
            }

            tutorial_ui_open_ = false;
            tutorial_released_ = true;
            ++tutorial_release_count_;
            return RuntimeShootFlowRestoreObservation::TutorialReleased;
        }

        auto observe_player_state(
                bool cinematic,
                bool known,
                std::chrono::steady_clock::time_point now,
                std::chrono::milliseconds settle_delay) -> RuntimeShootFlowRestoreObservation
        {
            if (!tutorial_released_ || consumed_for_pawn_ || pending_ || !known)
            {
                return RuntimeShootFlowRestoreObservation::Ignored;
            }

            if (cinematic)
            {
                if (post_tutorial_cinematic_seen_)
                {
                    return RuntimeShootFlowRestoreObservation::Ignored;
                }
                post_tutorial_cinematic_seen_ = true;
                ++post_tutorial_cinematic_count_;
                return RuntimeShootFlowRestoreObservation::PostTutorialCinematicSeen;
            }

            if (!post_tutorial_cinematic_seen_)
            {
                return RuntimeShootFlowRestoreObservation::Ignored;
            }

            queue_restore(now, settle_delay);
            ++cinematic_exit_queue_count_;
            return RuntimeShootFlowRestoreObservation::RestoreQueued;
        }

        auto reset_for_pawn() -> void
        {
            special_battle_seen_ = false;
            tutorial_ui_open_ = false;
            tutorial_released_ = false;
            post_tutorial_cinematic_seen_ = false;
            pending_ = false;
            consumed_for_pawn_ = false;
            restore_not_before_ = {};
        }

        [[nodiscard]] auto pending() const -> bool
        {
            return pending_;
        }

        [[nodiscard]] auto consumed_for_pawn() const -> bool
        {
            return consumed_for_pawn_;
        }

        [[nodiscard]] auto ready(
                std::chrono::steady_clock::time_point now) const -> bool
        {
            return pending_ && !consumed_for_pawn_ && now >= restore_not_before_;
        }

        auto consume_pending(std::chrono::steady_clock::time_point now) -> bool
        {
            if (!ready(now))
            {
                return false;
            }
            pending_ = false;
            consumed_for_pawn_ = true;
            ++restore_attempt_count_;
            return true;
        }

        auto record_restore_result(bool restored) -> void
        {
            if (restored)
            {
                ++restore_success_count_;
            }
        }

        [[nodiscard]] auto special_battle_count() const -> uint64_t
        {
            return special_battle_count_;
        }

        [[nodiscard]] auto tutorial_open_count() const -> uint64_t
        {
            return tutorial_open_count_;
        }

        [[nodiscard]] auto tutorial_release_count() const -> uint64_t
        {
            return tutorial_release_count_;
        }

        [[nodiscard]] auto post_tutorial_cinematic_count() const -> uint64_t
        {
            return post_tutorial_cinematic_count_;
        }

        [[nodiscard]] auto cinematic_exit_queue_count() const -> uint64_t
        {
            return cinematic_exit_queue_count_;
        }

        [[nodiscard]] auto restore_attempt_count() const -> uint64_t
        {
            return restore_attempt_count_;
        }

        [[nodiscard]] auto restore_success_count() const -> uint64_t
        {
            return restore_success_count_;
        }

    private:
        auto queue_restore(
                std::chrono::steady_clock::time_point now,
                std::chrono::milliseconds settle_delay) -> void
        {
            pending_ = true;
            restore_not_before_ = now + settle_delay;
        }

        bool special_battle_seen_{false};
        bool tutorial_ui_open_{false};
        bool tutorial_released_{false};
        bool post_tutorial_cinematic_seen_{false};
        bool pending_{false};
        bool consumed_for_pawn_{false};
        std::chrono::steady_clock::time_point restore_not_before_{};
        uint64_t special_battle_count_{0};
        uint64_t tutorial_open_count_{0};
        uint64_t tutorial_release_count_{0};
        uint64_t post_tutorial_cinematic_count_{0};
        uint64_t cinematic_exit_queue_count_{0};
        uint64_t restore_attempt_count_{0};
        uint64_t restore_success_count_{0};
    };
}
