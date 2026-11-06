#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UFunction;
}

namespace kh3ap
{
    class RuntimeRetryBlueprintProbeState;
    class RuntimeRetryPointProbeState;

    struct RuntimeRetryProbeConfig
    {
        bool blueprint_observer_enabled{};
        bool retry_point_direct_hooks_enabled{};
        bool create_retry_point_hook_enabled{};
        bool retry_point_process_event_observer_enabled{};
        bool discard_retry_point_hook_enabled{};
        RuntimeRetryBlueprintProbeState& blueprint_state;
        RuntimeRetryPointProbeState& retry_point_state;
        std::atomic<RC::Unreal::UFunction*>& retry_anim_notify_target;
        std::atomic<RC::Unreal::UFunction*>& retry_logic_begin_play_target;
        std::atomic<RC::Unreal::UFunction*>& create_retry_point_target;
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::wstring_view protocol_name{};
        int protocol_version{};
        std::wstring_view build_stamp{};
        std::wstring_view retry_anim_notify_path{};
        std::wstring_view retry_logic_begin_play_path{};
        std::wstring_view retry_blueprint_latest_filename{};
        std::wstring_view retry_blueprint_events_filename{};
        std::wstring_view retry_point_latest_filename{};
        std::wstring_view retry_point_events_filename{};
    };

    class RuntimeRetryProbeService
    {
    public:
        explicit RuntimeRetryProbeService(RuntimeRetryProbeConfig config);
        ~RuntimeRetryProbeService();

        RuntimeRetryProbeService(const RuntimeRetryProbeService&) = delete;
        auto operator=(const RuntimeRetryProbeService&)
                -> RuntimeRetryProbeService& = delete;
        RuntimeRetryProbeService(RuntimeRetryProbeService&&) noexcept;
        auto operator=(RuntimeRetryProbeService&&) noexcept
                -> RuntimeRetryProbeService&;

        auto refresh_blueprint_process_observer_targets(bool log_status) -> void;
        auto flush_deferred_blueprint_events() -> void;
        auto flush_deferred_retry_point_events() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
