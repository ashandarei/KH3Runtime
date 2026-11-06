#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <chrono>
#include <stop_token>

namespace kh3ap { struct RuntimeItemGrantBatchRequest; }

namespace kh3ap::pipe
{
    // JSON channel names are retained so existing domain parsers stay unchanged.
    // All operations below touch memory only. Only the worker owns pipe handles.
    auto manages(const std::filesystem::path& path) -> bool;
    auto durable(const std::filesystem::path& path) -> bool;
    auto read(const std::filesystem::path& path) -> std::optional<std::string>;
    // Worker-only, bounded and cancellable. Consume a transient reply without
    // echoing its payload back to the client or retaining a published copy.
    auto wait_and_take(const std::filesystem::path& path, std::stop_token cancellation,
                       std::chrono::milliseconds timeout) -> std::optional<std::string>;
    // Parsed by the receiving worker outside the shared IPC mutex. Immutable
    // values only; live binding and gameplay safety stay on the game thread.
    auto grant_batch(const std::filesystem::path& path)
        -> std::pair<uint64_t, std::shared_ptr<const RuntimeItemGrantBatchRequest>>;
    auto write(const std::filesystem::path& path, std::string_view value) -> bool;
    // Native worker-owned priority command; parsed off the game thread and
    // consumed by the existing delivery queue without a client round trip.
    auto submit_native_grant(const std::filesystem::path& path, std::string_view value) -> bool;
    auto erase(const std::filesystem::path& path) -> void;
    auto revision(const std::filesystem::path& path) -> uint64_t;
    // Unlike revision() (which also backs existence checks), retain inbound
    // authority after deletion. read() then returns an authoritative absence.
    auto input_revision(const std::filesystem::path& path) -> uint64_t;
    auto take_updates() -> bool;
    auto take_grant_updates() -> bool;
    auto notify_grants() -> void;
    // Non-consuming change notification for independent game-thread services.
    // Local publications and repeated identical inbound values do not advance it.
    auto input_generation() -> uint64_t;
    // Auto uses local TCP under Wine/Proton, named pipes on native Windows.
    enum class Transport { Auto, NamedPipe, LocalTcp };
    auto start(const std::filesystem::path& root, Transport transport = Transport::Auto) -> void;
    auto stop() -> void;
}
