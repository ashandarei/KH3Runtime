#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
class RuntimePortalService {
public:
    RuntimePortalService(std::function<std::optional<std::filesystem::path>()> bridge,
                         std::function<RC::Unreal::UObject*()> pawn);
    ~RuntimePortalService();
    static void on_map_jump();
    static void on_map_jump_canceled();
    // Game-thread receipt/lifecycle signals; no timed ownership refresh.
    static void notify_party_member_received();
    static void notify_context_ready();
    static auto gate_opener_menu() -> std::wstring;
    static auto use_gate_opener(int portal) -> bool;
    void tick(std::chrono::steady_clock::time_point now);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
