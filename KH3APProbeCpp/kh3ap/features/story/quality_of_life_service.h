#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeQualityOfLifeConfig
    {
        RuntimeState& runtime_state;
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
        std::wstring_view get_game_flag_path{};
        std::wstring_view get_game_flag_boolean_path{};
        std::wstring_view set_game_flag_path{};
        std::wstring_view set_game_flag_boolean_path{};
        std::wstring_view blueprint_library_default_object_path{};
        bool enabled{true};
        bool debug_logging{false};
        // Only unavailable settings or a failed pending rule may retry.
        std::chrono::steady_clock::duration retry_interval{
                std::chrono::seconds(1)};
    };

    class RuntimeQualityOfLifeService
    {
    public:
        explicit RuntimeQualityOfLifeService(
                RuntimeQualityOfLifeConfig config);
        ~RuntimeQualityOfLifeService();

        RuntimeQualityOfLifeService(
                const RuntimeQualityOfLifeService&) = delete;
        auto operator=(const RuntimeQualityOfLifeService&)
                -> RuntimeQualityOfLifeService& = delete;
        RuntimeQualityOfLifeService(
                RuntimeQualityOfLifeService&&) noexcept;
        auto operator=(RuntimeQualityOfLifeService&&) noexcept
                -> RuntimeQualityOfLifeService&;

        auto tick(std::chrono::steady_clock::time_point now) -> void;
        auto request_refresh() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
