#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeControllerAbilityService;
    class RuntimePlayerContextService;
    class RuntimeState;

    struct RuntimeMapActorLifecycleConfig
    {
        RuntimeState& runtime_state;
        RuntimePlayerContextService& player_context;
        RuntimeControllerAbilityService& controller_abilities;
    };

    class RuntimeMapActorLifecycleService
    {
    public:
        using PhaseMarker = std::function<void(std::wstring_view)>;

        explicit RuntimeMapActorLifecycleService(RuntimeMapActorLifecycleConfig config);
        ~RuntimeMapActorLifecycleService();

        RuntimeMapActorLifecycleService(const RuntimeMapActorLifecycleService&) = delete;
        auto operator=(const RuntimeMapActorLifecycleService&)
                -> RuntimeMapActorLifecycleService& = delete;
        RuntimeMapActorLifecycleService(RuntimeMapActorLifecycleService&&) noexcept;
        auto operator=(RuntimeMapActorLifecycleService&&) noexcept
                -> RuntimeMapActorLifecycleService&;

        auto reset() -> void;
        auto has_pending_work() const -> bool;
        auto notify_context_ready() -> void;
        auto cache_bx_portal_class(RC::Unreal::UObject* object) -> bool;
        auto cache_he02_death_mirror_class(RC::Unreal::UObject* object) -> bool;
        auto process(
                const std::optional<std::filesystem::path>& bridge_dir,
                std::chrono::steady_clock::time_point now,
                const PhaseMarker& mark_phase) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
