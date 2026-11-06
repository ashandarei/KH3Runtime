#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap::lucky_marks
{
    inline constexpr auto journal_file = L"lucky_mark_progress.json";
    inline constexpr auto state_file = channels::runtime_lucky_mark_checks_file;
    struct View
    {
        bool ready{};
        std::filesystem::path bridge;
        std::wstring seed;
        int64_t slot{};
        std::vector<int32_t> milestones;
        std::string error;
    };
    auto location_id(std::wstring_view token, uint32_t fname_number = 0) -> std::optional<int64_t>;
    // Hooks enqueue values only. The worker validates seed metadata, commits
    // checkpoints and publishes completed checks; it never touches UObjects.
    auto start(const std::filesystem::path& root) -> void;
    auto stop() -> void;
    auto record(const std::filesystem::path& root, std::wstring_view token, int32_t result, uint32_t fname_number = 0) -> void;
    auto view() -> std::shared_ptr<const View>;
    auto revision() -> uint64_t;
}
