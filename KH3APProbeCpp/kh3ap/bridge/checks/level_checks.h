#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once
#include <cstdint>
#include <filesystem>

namespace kh3ap::level_checks
{
    inline constexpr auto journal_file = L"level_up_progress.json";
    inline constexpr auto state_file = channels::runtime_level_up_checks_file;
    auto start(const std::filesystem::path& root) -> void;
    auto stop() -> void;
    // Only the game-thread Sora ownership gate may call this. Values are
    // coalesced; the worker owns threshold selection, JSON and durable I/O.
    auto observe(const std::filesystem::path& root, uint64_t level) -> void;
}
