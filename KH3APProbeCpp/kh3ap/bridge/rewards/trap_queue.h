#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once
#include "kh3ap/core/types.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace kh3ap::native_traps
{
    inline constexpr auto request_file = channels::runtime_trap_queue_request_file;
    inline constexpr auto ack_file = channels::runtime_trap_queue_ack_file;
    inline constexpr auto state_file = channels::runtime_trap_queue_state_file;
    inline constexpr auto journal_file = L"runtime_native_trap_queue.json";
    struct View
    {
        bool ready{};
        std::wstring seed;
        int64_t slot{};
        size_t pending{};
        size_t accepted{};
        std::string error;
        std::optional<RuntimeItemGrantRequest> head;
    };
    auto is_trap(const RuntimeItemGrantRequest& request) -> bool;
    // Only the worker reads/writes the native journal. Snapshots contain values;
    // no UObject or game function is accessed from that worker.
    auto start(const std::filesystem::path& root) -> void;
    auto stop() -> void;
    auto view() -> std::shared_ptr<const View>;
    auto submit(std::shared_ptr<const RuntimeItemGrantBatchRequest> request) -> void;
    auto reject(std::shared_ptr<const RuntimeItemGrantBatchRequest> request, std::string reason) -> void;
    auto applied(int64_t index) -> void;
    auto waiting(std::string reason) -> void;
}
