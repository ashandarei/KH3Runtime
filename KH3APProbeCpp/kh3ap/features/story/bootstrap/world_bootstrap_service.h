#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include "kh3ap/core/performance_build.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimePlayerContextService;

    struct RuntimeWorldBootstrapConfig
    {
        RuntimePlayerContextService& player_context;
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver;
        bool enabled{true};
        bool debug_logging{false};
        std::chrono::milliseconds retry_interval{250};
        std::chrono::milliseconds asset_load_retry_interval{2000};
    };

    class RuntimeWorldBootstrapService
    {
    public:
        explicit RuntimeWorldBootstrapService(
                RuntimeWorldBootstrapConfig config);
        ~RuntimeWorldBootstrapService();

        RuntimeWorldBootstrapService(
                const RuntimeWorldBootstrapService&) = delete;
        auto operator=(const RuntimeWorldBootstrapService&)
                -> RuntimeWorldBootstrapService& = delete;
        RuntimeWorldBootstrapService(
                RuntimeWorldBootstrapService&&) noexcept;
        auto operator=(RuntimeWorldBootstrapService&&) noexcept
                -> RuntimeWorldBootstrapService&;

        auto tick(std::chrono::steady_clock::time_point now) -> void;
        auto reset() -> void;
        auto world_ready() -> void;
        auto cache_generated_class(RC::Unreal::UObject* object) -> bool;
        auto set_battle_level_map_visible(bool visible) -> bool;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto diagnostic_snapshot(bool remove_widget) -> std::string;
#endif

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
