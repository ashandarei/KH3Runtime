#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
namespace kh3ap {
class RuntimeVBonusTrackingService {
public:
    using BridgeResolver = std::function<std::optional<std::filesystem::path>()>;
    explicit RuntimeVBonusTrackingService(BridgeResolver resolver);
    ~RuntimeVBonusTrackingService();
    // Game-thread drain: idle calls inspect notification bits only.
    void process_events();
    // One reconciliation per explicit world/load-ready event, never a timer.
    void world_ready();
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
