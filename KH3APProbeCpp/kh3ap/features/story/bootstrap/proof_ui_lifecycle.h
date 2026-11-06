#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
namespace RC::Unreal
{
    class UObject;
    class UClass;
} // namespace RC::Unreal
namespace kh3ap
{
    // Owns the widget and its event subscriptions. Callbacks record address-only
    // observations; creation, viewport checks and retirement stay on the game thread.
    class ProofUiLifecycle
    {
    public:
        explicit ProofUiLifecycle(std::chrono::milliseconds retry_interval);
        ~ProofUiLifecycle();
        auto install_lifecycle_events() -> bool;
        auto process_events() -> bool;
        auto refresh_viewport() -> bool;
        auto ensure_widget(RC::Unreal::UObject* pawn, RC::Unreal::UClass* widget_class,
                           std::chrono::steady_clock::time_point now) -> bool;
        auto reset(bool clear_events) -> void;
        auto request_viewport_check() -> void;
        auto set_battle_level_map_visible(bool visible) -> bool;
        auto remove_widget() -> bool;
        auto widget() const -> RC::Unreal::UObject*;
        auto added() const -> bool;
        auto lifecycle_installed() const -> bool;
        auto widgets_created() const -> uint64_t;
        static auto in_viewport(RC::Unreal::UObject* widget) -> std::optional<bool>;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace kh3ap
