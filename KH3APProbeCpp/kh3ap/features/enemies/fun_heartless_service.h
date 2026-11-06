#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
// True means this encounter owns its roster; leave its source enemy classes intact.
bool prepare_fun_heartless_serialized_entity(RC::Unreal::UObject*) noexcept;
struct RuntimeFunHeartlessConfig {
    std::function<std::optional<std::filesystem::path>()> bridge_dir;
    std::function<RC::Unreal::UObject*()> sora;
    std::function<bool()> battle_active;
};
class RuntimeFunHeartlessService {
public:
    explicit RuntimeFunHeartlessService(RuntimeFunHeartlessConfig);
    ~RuntimeFunHeartlessService();
    void tick();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
