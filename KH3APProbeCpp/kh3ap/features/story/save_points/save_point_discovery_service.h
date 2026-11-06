#pragma once

#include <chrono>
#include <memory>

namespace kh3ap
{
    struct RuntimeSavePointSpawnDeps;

    class RuntimeSavePointDiscoveryService
    {
    public:
        RuntimeSavePointDiscoveryService();
        ~RuntimeSavePointDiscoveryService();
        auto install_once() -> void;
        auto has_pending_work() const -> bool;
        auto process(const RuntimeSavePointSpawnDeps& deps,
                     std::chrono::steady_clock::time_point now) -> void;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
