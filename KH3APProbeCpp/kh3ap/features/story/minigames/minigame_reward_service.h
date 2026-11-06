#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
class RuntimeMinigameRewardService {
public:
    explicit RuntimeMinigameRewardService(std::function<std::optional<std::filesystem::path>()>,
                                         std::function<RC::Unreal::UObject*()> playable_sora);
    ~RuntimeMinigameRewardService();
    void install_once();
    void tick();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    static RC::Unreal::UObject* diagnostic_controller();
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
