#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace kh3ap::deathlink
{
    inline constexpr auto journal_file = L"runtime_native_deathlink.json";
    inline constexpr auto state_file = channels::runtime_deathlink_state_file;
    inline constexpr auto control_file = channels::runtime_deathlink_control_file;
    struct View
    {
        bool ready{}, fenced{}, awaiting_apply{}, recovery_pending{}, manual_active{};
        std::string error;
    };
    // Values-only worker. Game-thread delivery retains all UObject safety gates.
    auto start(const std::filesystem::path& root) -> void;
    auto stop() -> void;
    auto observe(std::string_view channel, std::string_view json) -> void;
    // Values-only observation from owned swap pawn lifecycle/state events.
    auto observe_manual_player(std::string player_key, bool dead, bool ready) -> void;
    auto disconnected() -> void;
    auto view() -> std::shared_ptr<const View>;
}
