#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace kh3ap
{
    struct RuntimeOlympusColiseumCompletionConfig
    {
        bool enabled{};
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
        std::function<void(std::wstring_view, std::wstring_view)>
                runtime_grant_transition_started{};
    };

    class RuntimeOlympusColiseumCompletionService
    {
    public:
        explicit RuntimeOlympusColiseumCompletionService(
                RuntimeOlympusColiseumCompletionConfig config);
        ~RuntimeOlympusColiseumCompletionService();

        RuntimeOlympusColiseumCompletionService(
                const RuntimeOlympusColiseumCompletionService&) = delete;
        auto operator=(const RuntimeOlympusColiseumCompletionService&)
                -> RuntimeOlympusColiseumCompletionService& = delete;
        RuntimeOlympusColiseumCompletionService(
                RuntimeOlympusColiseumCompletionService&&) noexcept;
        auto operator=(RuntimeOlympusColiseumCompletionService&&) noexcept
                -> RuntimeOlympusColiseumCompletionService&;

        auto process_progress_save(const std::filesystem::path& save_path)
                -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
