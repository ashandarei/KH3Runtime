#include "kh3ap/features/player/context/context_state.h"

#include "kh3ap/features/player/context/context_resolver.h"

namespace kh3ap
{
    namespace
    {
        constexpr std::wstring_view playable_pawn_context_hook{L"RuntimePlayablePawnContext"};
    }

    auto RuntimeContextState::current_snapshot() const -> std::optional<RuntimeContextSnapshot>
    {
        std::lock_guard lock(current_snapshot_mutex_);
        if (current_snapshot_.hook.empty() && current_snapshot_.world.empty() && current_snapshot_.outer.empty())
        {
            return std::nullopt;
        }
        return current_snapshot_;
    }

    auto RuntimeContextState::current_summary() const -> std::optional<std::wstring>
    {
        std::lock_guard lock(current_snapshot_mutex_);
        if (current_snapshot_.hook.empty() && current_snapshot_.world.empty() && current_snapshot_.outer.empty())
        {
            return std::nullopt;
        }
        return current_summary_;
    }

    auto RuntimeContextState::current_snapshot_age(std::chrono::steady_clock::time_point now) const
            -> std::optional<std::chrono::steady_clock::duration>
    {
        std::lock_guard lock(current_snapshot_mutex_);
        if (current_snapshot_.hook.empty() && current_snapshot_.world.empty() && current_snapshot_.outer.empty())
        {
            return std::nullopt;
        }
        if (current_snapshot_set_at_.time_since_epoch().count() == 0)
        {
            return std::nullopt;
        }
        return now - current_snapshot_set_at_;
    }

    auto RuntimeContextState::set_current_snapshot(
            const RuntimeContextSnapshot& snapshot,
            std::wstring_view last_package) -> void
    {
        std::lock_guard lock(current_snapshot_mutex_);
        current_snapshot_ = snapshot;
        current_summary_ = RuntimeContextResolver::summarize(snapshot);
        current_snapshot_set_at_ = std::chrono::steady_clock::now();
        last_frame_context_package_ = std::wstring{last_package};
    }

    auto RuntimeContextState::update_current_snapshot_and_detect_package_change(
            const RuntimeContextSnapshot& snapshot,
            std::wstring_view current_package_path) -> bool
    {
        std::lock_guard lock(current_snapshot_mutex_);
        current_snapshot_ = snapshot;
        current_summary_ = RuntimeContextResolver::summarize(snapshot);
        current_snapshot_set_at_ = std::chrono::steady_clock::now();
        if (current_package_path == last_frame_context_package_)
        {
            return false;
        }

        last_frame_context_package_ = std::wstring{current_package_path};
        reset_playable_publish_locked();
        return true;
    }

    auto RuntimeContextState::should_skip_playable_publish(
            std::wstring_view context_key,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) const -> bool
    {
        std::lock_guard lock(current_snapshot_mutex_);
        return current_snapshot_.hook == playable_pawn_context_hook &&
               context_key == last_playable_context_key_ &&
               last_playable_context_at_.time_since_epoch().count() != 0 &&
               (now - last_playable_context_at_) < interval;
    }

    auto RuntimeContextState::should_skip_playable_publish_for_object(
            const void* object,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration interval) const -> bool
    {
        if (!object)
        {
            return false;
        }

        std::lock_guard lock(current_snapshot_mutex_);
        return current_snapshot_.hook == playable_pawn_context_hook &&
               object == last_playable_context_object_ &&
               last_playable_context_at_.time_since_epoch().count() != 0 &&
               (now - last_playable_context_at_) < interval;
    }

    auto RuntimeContextState::last_playable_context_object() const -> const void*
    {
        std::lock_guard lock(current_snapshot_mutex_);
        return last_playable_context_object_;
    }

    auto RuntimeContextState::mark_playable_published(
            std::wstring_view context_key,
            std::chrono::steady_clock::time_point now,
            const void* object) -> void
    {
        std::lock_guard lock(current_snapshot_mutex_);
        last_playable_context_key_ = std::wstring{context_key};
        last_playable_context_at_ = now;
        last_playable_context_object_ = object;
    }

    auto RuntimeContextState::reset_playable_publish() -> void
    {
        std::lock_guard lock(current_snapshot_mutex_);
        reset_playable_publish_locked();
    }

    auto RuntimeContextState::reset_playable_publish_locked() -> void
    {
        last_playable_context_key_.clear();
        last_playable_context_at_ = {};
        last_playable_context_object_ = nullptr;
    }
}
