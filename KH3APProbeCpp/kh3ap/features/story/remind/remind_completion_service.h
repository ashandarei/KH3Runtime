#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeRemindCompletionConfig
    {
        RuntimeState& runtime_state;
        std::wstring_view get_game_flag_path{};
        std::wstring_view set_game_flag_path{};
        std::wstring_view blueprint_library_default_object_path{};
        bool enabled{true};
        bool debug_logging{false};
    };

    class RuntimeRemindCompletionService
    {
    public:
        explicit RuntimeRemindCompletionService(RuntimeRemindCompletionConfig config);
        ~RuntimeRemindCompletionService();

        RuntimeRemindCompletionService(const RuntimeRemindCompletionService&) = delete;
        auto operator=(const RuntimeRemindCompletionService&) -> RuntimeRemindCompletionService& = delete;
        RuntimeRemindCompletionService(RuntimeRemindCompletionService&&) noexcept;
        auto operator=(RuntimeRemindCompletionService&&) noexcept -> RuntimeRemindCompletionService&;

        auto reset() -> void;
        auto has_pending_work() const -> bool;
        auto process(
                const std::optional<std::filesystem::path>& bridge_dir,
                std::wstring_view context_summary) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
