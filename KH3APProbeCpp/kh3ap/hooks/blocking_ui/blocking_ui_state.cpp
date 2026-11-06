#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"

namespace kh3ap
{
    auto RuntimeBlockingUiState::update(
            bool active,
            std::wstring_view reason,
            std::wstring_view context,
            std::wstring_view event,
            int64_t callback_id,
            std::chrono::steady_clock::time_point now) -> bool
    {
        std::lock_guard lock{mutex_};
        const bool changed =
                snapshot_.active != active ||
                snapshot_.reason != reason ||
                snapshot_.context != context ||
                snapshot_.event != event ||
                snapshot_.callback_id != callback_id;
        if (snapshot_.active != active) request_music_update(MusicUpdate::Menu);
        snapshot_ = RuntimeBlockingUiSnapshot{
                .active = active,
                .reason = std::wstring{reason},
                .context = std::wstring{context},
                .event = std::wstring{event},
                .callback_id = callback_id,
                .updated_at = now};
        return changed;
    }

    auto RuntimeBlockingUiState::snapshot() const -> RuntimeBlockingUiSnapshot
    {
        std::lock_guard lock{mutex_};
        return snapshot_;
    }

    auto RuntimeBlockingUiFocusHandoffState::observe_focus_in(
            uintptr_t context_id) -> RuntimeBlockingUiFocusUpdate
    {
        if (context_id != 0)
        {
            focused_contexts_.insert(context_id);
        }
        release_not_before_.reset();
        return RuntimeBlockingUiFocusUpdate{
                .decision = RuntimeBlockingUiFocusDecision::publish_active,
                .focused_context_count = focused_contexts_.size(),
                .release_pending = false};
    }

    auto RuntimeBlockingUiFocusHandoffState::observe_focus_out(
            uintptr_t context_id,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration handoff_grace)
            -> RuntimeBlockingUiFocusUpdate
    {
        if (context_id != 0)
        {
            focused_contexts_.erase(context_id);
        }
        if (!focused_contexts_.empty())
        {
            release_not_before_.reset();
            return RuntimeBlockingUiFocusUpdate{
                    .decision =
                            RuntimeBlockingUiFocusDecision::publish_active,
                    .focused_context_count = focused_contexts_.size(),
                    .release_pending = false};
        }

        release_not_before_ = now + handoff_grace;
        return RuntimeBlockingUiFocusUpdate{
                .decision = RuntimeBlockingUiFocusDecision::defer_inactive,
                .focused_context_count = 0,
                .release_pending = true};
    }

    auto RuntimeBlockingUiFocusHandoffState::settle(
            std::chrono::steady_clock::time_point now)
            -> RuntimeBlockingUiFocusUpdate
    {
        if (!release_not_before_.has_value() ||
            now < *release_not_before_)
        {
            return RuntimeBlockingUiFocusUpdate{
                    .decision = RuntimeBlockingUiFocusDecision::none,
                    .focused_context_count = focused_contexts_.size(),
                    .release_pending = release_not_before_.has_value()};
        }

        release_not_before_.reset();
        if (!focused_contexts_.empty())
        {
            return RuntimeBlockingUiFocusUpdate{
                    .decision = RuntimeBlockingUiFocusDecision::none,
                    .focused_context_count = focused_contexts_.size(),
                    .release_pending = false};
        }
        return RuntimeBlockingUiFocusUpdate{
                .decision = RuntimeBlockingUiFocusDecision::publish_inactive,
                .focused_context_count = 0,
                .release_pending = false};
    }

    auto RuntimeBlockingUiFocusHandoffState::reset() -> void
    {
        focused_contexts_.clear();
        release_not_before_.reset();
    }

    auto RuntimeBlockingUiFocusHandoffState::release_pending() const -> bool
    {
        return release_not_before_.has_value();
    }
}
