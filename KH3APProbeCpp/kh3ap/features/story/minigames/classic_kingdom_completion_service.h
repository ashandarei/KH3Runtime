#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace kh3ap
{
    struct RuntimeClassicKingdomCompletionConfig
    {
        bool enabled{};
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
        std::function<void(std::wstring_view, std::wstring_view)>
                runtime_grant_transition_started{};
    };

    class RuntimeClassicKingdomCompletionService
    {
    public:
        explicit RuntimeClassicKingdomCompletionService(
                RuntimeClassicKingdomCompletionConfig config);
        ~RuntimeClassicKingdomCompletionService();

        RuntimeClassicKingdomCompletionService(
                const RuntimeClassicKingdomCompletionService&) = delete;
        auto operator=(const RuntimeClassicKingdomCompletionService&)
                -> RuntimeClassicKingdomCompletionService& = delete;
        RuntimeClassicKingdomCompletionService(
                RuntimeClassicKingdomCompletionService&&) noexcept;
        auto operator=(RuntimeClassicKingdomCompletionService&&) noexcept
                -> RuntimeClassicKingdomCompletionService&;

        auto install_once() -> void;
        auto uninstall() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
