#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace kh3ap
{
    struct RuntimePlayerReacquireSnapshot
    {
        bool ignored_map_active{false};
        std::wstring ignored_map_context{};
        bool reacquire_armed{false};
        std::chrono::steady_clock::time_point last_reacquire_attempt{};
    };

    class RuntimePlayerReacquireState
    {
    public:
        auto enter_ignored_map(std::wstring_view context) -> bool;
        auto leave_ignored_map_from_validated_pawn(
                std::wstring_view pawn_name) -> bool;

        auto arm(std::chrono::steady_clock::time_point now) -> void;
        auto disarm() -> bool;
        auto suppress() -> void;
        auto stop_for_invalid_controller() -> void;
        auto should_attempt(
                std::chrono::steady_clock::time_point now,
                std::chrono::steady_clock::duration interval) -> bool;

        [[nodiscard]] auto ignored_map_active() const -> bool;
        [[nodiscard]] auto reacquire_armed() const -> bool;
        [[nodiscard]] auto snapshot() const -> RuntimePlayerReacquireSnapshot;

    private:
        RuntimePlayerReacquireSnapshot snapshot_{};
    };
}
