#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace RC::Unreal { class UObject; }
namespace kh3ap {
struct RuntimeMinimapContextConfig {
    std::function<RC::Unreal::UObject*()> pawn;
    std::function<bool()> gameplay_ready;
    std::function<std::optional<std::filesystem::path>()> bridge_dir;
};
class RuntimeMinimapContextService {
public:
    explicit RuntimeMinimapContextService(RuntimeMinimapContextConfig);
    ~RuntimeMinimapContextService();
    auto install() -> void;
    auto uninstall() -> void;
    auto flush_pending() -> void;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
