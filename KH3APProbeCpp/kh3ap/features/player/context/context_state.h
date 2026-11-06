#pragma once

#include "kh3ap/core/types.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeContextState
    {
    public:
        static constexpr std::chrono::seconds playable_republish_interval{30};

        [[nodiscard]] auto current_snapshot() const -> std::optional<RuntimeContextSnapshot>;
        [[nodiscard]] auto current_summary() const -> std::optional<std::wstring>;
        [[nodiscard]] auto current_snapshot_age(std::chrono::steady_clock::time_point now) const
                -> std::optional<std::chrono::steady_clock::duration>;
        auto set_current_snapshot(const RuntimeContextSnapshot& snapshot, std::wstring_view last_package) -> void;
        auto update_current_snapshot_and_detect_package_change(
                const RuntimeContextSnapshot& snapshot,
                std::wstring_view current_package_path) -> bool;

        [[nodiscard]] auto should_skip_playable_publish(
                std::wstring_view context_key,
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) const -> bool;
        [[nodiscard]] auto should_skip_playable_publish_for_object(
                const void* object,
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) const -> bool;
        [[nodiscard]] auto last_playable_context_object() const -> const void*;
        auto mark_playable_published(
                std::wstring_view context_key,
                std::chrono::steady_clock::time_point now,
                const void* object = nullptr) -> void;
        auto reset_playable_publish() -> void;

    private:
        auto reset_playable_publish_locked() -> void;

        mutable std::mutex current_snapshot_mutex_{};
        RuntimeContextSnapshot current_snapshot_{};
        std::wstring current_summary_{};
        std::chrono::steady_clock::time_point current_snapshot_set_at_{};
        std::wstring last_frame_context_package_{};
        std::wstring last_playable_context_key_{};
        std::chrono::steady_clock::time_point last_playable_context_at_{};
        const void* last_playable_context_object_{nullptr};
    };
}
