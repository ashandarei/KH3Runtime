#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace kh3ap {
class RuntimeSaveSpaceService {
public:
    explicit RuntimeSaveSpaceService(std::function<std::optional<std::filesystem::path>()> bridge);
    ~RuntimeSaveSpaceService();
    void install_once();
    void tick(std::chrono::steady_clock::time_point now);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
