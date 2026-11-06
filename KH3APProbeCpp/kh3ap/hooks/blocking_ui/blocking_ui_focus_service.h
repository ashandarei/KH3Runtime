#pragma once

#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace kh3ap
{
    struct RuntimeBlockingUiHookConfig;

    // Owns focus identity, handoff timing, and publication to the shared
    // blocking-UI state. It is deliberately Unreal-agnostic: the hook service
    // supplies only a context identity and already-resolved names.
    class RuntimeBlockingUiFocusService
    {
    public:
        explicit RuntimeBlockingUiFocusService(
                const RuntimeBlockingUiHookConfig& config);

        auto observe_engine_pause(
                bool active,
                std::wstring_view context_name,
                std::wstring_view event_name) -> void;
        auto observe_ui_lifecycle(
                uintptr_t context_id,
                std::wstring_view context_name,
                std::wstring_view event_name,
                bool focus_in,
                std::chrono::steady_clock::time_point now) -> void;
        auto process_pending_release(
                std::chrono::steady_clock::time_point now) -> void;
        auto reset() -> void;

    private:
        [[nodiscard]] auto gameplay_event_allowed() const -> bool;
        auto publish_transition(
                bool active,
                std::wstring_view context_name,
                std::wstring_view event_name) -> void;
        auto reset_locked() -> void;

        const RuntimeBlockingUiHookConfig& config_;
        std::recursive_mutex mutex_{};
        RuntimeBlockingUiFocusHandoffState handoff_state_{};
        std::atomic<bool> release_pending_{false};
        uint64_t lifecycle_generation_{};
        uint64_t pending_release_generation_{};
        std::wstring pending_release_context_{};
        std::wstring pending_release_event_{};
    };
}
