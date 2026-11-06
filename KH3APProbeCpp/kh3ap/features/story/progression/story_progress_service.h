#pragma once

#include "kh3ap/core/types.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeStoryProgressConfig
    {
        RuntimeState& runtime_state;
        std::function<std::optional<RuntimeContextSnapshot>()> frame_context_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<bool()> guardians_and_mickey_walk_skip_enabled_resolver{};
    };

    class RuntimeStoryProgressService
    {
    public:
        explicit RuntimeStoryProgressService(RuntimeStoryProgressConfig config);
        ~RuntimeStoryProgressService();

        RuntimeStoryProgressService(const RuntimeStoryProgressService&) = delete;
        auto operator=(const RuntimeStoryProgressService&)
                -> RuntimeStoryProgressService& = delete;
        RuntimeStoryProgressService(RuntimeStoryProgressService&&) noexcept;
        auto operator=(RuntimeStoryProgressService&&) noexcept
                -> RuntimeStoryProgressService&;

        auto process_guardians_of_light_skip(
                std::chrono::steady_clock::time_point now) -> void;
        auto arm_caribbean_chart_unlock(std::wstring_view starting_player_context) -> void;
        auto mark_caribbean_chart_map_ready(std::wstring_view map_ready_context) -> void;
        auto process_caribbean_chart_unlock(
                std::chrono::steady_clock::time_point now) -> void;
        auto reset_map_lifecycle() -> void;
        auto notify_context_ready() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
