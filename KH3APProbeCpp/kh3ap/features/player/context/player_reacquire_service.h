#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    struct RuntimePlayerReacquireConfig
    {
        RuntimeState& runtime_state;
        std::function<bool(RC::Unreal::UObject*)> is_player_controller{};
        std::function<bool(
                RC::Unreal::UObject*,
                std::wstring_view,
                std::wstring_view,
                bool)> cache_pawn_from_controller{};
        std::function<void()> reset_battle_level_lifecycle{};
        std::function<void(std::wstring_view)> sync_battle_level_after_reacquire{};
        std::function<void()> clear_special_battle_pending{};
        std::function<void()> reset_map_lifecycle{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::chrono::steady_clock::duration reacquire_interval{};
    };

    // Owns the yt_50 ignore gate and the bounded cached-controller fallback
    // used when HandleStartingNewPlayer cannot immediately find Sora's pawn.
    class RuntimePlayerReacquireService
    {
    public:
        explicit RuntimePlayerReacquireService(
                RuntimePlayerReacquireConfig config);
        ~RuntimePlayerReacquireService();

        RuntimePlayerReacquireService(
                const RuntimePlayerReacquireService&) = delete;
        auto operator=(const RuntimePlayerReacquireService&)
                -> RuntimePlayerReacquireService& = delete;
        RuntimePlayerReacquireService(RuntimePlayerReacquireService&&) noexcept;
        auto operator=(RuntimePlayerReacquireService&&) noexcept
                -> RuntimePlayerReacquireService&;

        auto enter_ignored_map(
                std::wstring_view observed_context,
                std::wstring_view source) -> void;
        auto leave_ignored_map_from_validated_pawn(
                std::wstring_view pawn_name,
                std::wstring_view source) -> void;
        auto arm(
                RC::Unreal::UObject* controller,
                std::wstring_view source) -> void;
        auto disarm(std::wstring_view source) -> void;
        auto suppress() -> void;
        auto process(std::chrono::steady_clock::time_point now) -> void;

        [[nodiscard]] auto ignored_map_active() const -> bool;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
