#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace kh3ap
{
    struct RuntimeBlockingUiSnapshot
    {
        bool active{false};
        std::wstring reason{};
        std::wstring context{};
        std::wstring event{};
        int64_t callback_id{-1};
        std::chrono::steady_clock::time_point updated_at{};
    };

    class RuntimeBlockingUiState
    {
    public:
        auto update(
                bool active,
                std::wstring_view reason,
                std::wstring_view context,
                std::wstring_view event,
                int64_t callback_id,
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) -> bool;

        [[nodiscard]] auto snapshot() const -> RuntimeBlockingUiSnapshot;

    private:
        mutable std::mutex mutex_{};
        RuntimeBlockingUiSnapshot snapshot_{};
    };

    enum class RuntimeBlockingUiFocusDecision : uint8_t
    {
        none,
        publish_active,
        defer_inactive,
        publish_inactive,
    };

    struct RuntimeBlockingUiFocusUpdate
    {
        RuntimeBlockingUiFocusDecision decision{
                RuntimeBlockingUiFocusDecision::none};
        size_t focused_context_count{};
        bool release_pending{};
    };

    // Tracks UI ownership by identity only. Stored context values are never
    // dereferenced, scanned, or validated; they only pair a panel's focus-in
    // with its focus-out. A short empty-set deadline absorbs panel-to-panel
    // focus handoffs before publishing the real inactive transition. This
    // state is a delivery guard; the transition itself does not dispatch work.
    class RuntimeBlockingUiFocusHandoffState
    {
    public:
        auto observe_focus_in(uintptr_t context_id)
                -> RuntimeBlockingUiFocusUpdate;
        auto observe_focus_out(
                uintptr_t context_id,
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration handoff_grace)
                -> RuntimeBlockingUiFocusUpdate;
        auto settle(std::chrono::steady_clock::time_point now)
                -> RuntimeBlockingUiFocusUpdate;
        auto reset() -> void;

        [[nodiscard]] auto release_pending() const -> bool;

    private:
        std::unordered_set<uintptr_t> focused_contexts_{};
        std::optional<std::chrono::steady_clock::time_point>
                release_not_before_{};
    };

}
