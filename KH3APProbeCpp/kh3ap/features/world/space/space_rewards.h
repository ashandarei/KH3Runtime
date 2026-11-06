#pragma once
#include <filesystem>
#include <string>
#include <memory>
#include <cstdint>
#include <ostream>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
class RuntimeSpaceRewards {
public:
    RuntimeSpaceRewards();
    ~RuntimeSpaceRewards();
    void configure(const std::filesystem::path&, const std::wstring&, int64_t slot, bool enabled);
    void request_recovery();
    void tick(RC::Unreal::UObject* world, bool loading_closed);
    // Called after a successful native mission check, before its caller awards loot.
    void prepare_mission_rewards();
    void mission_completed(uint8_t id);
    bool active() const;
    bool pending() const;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void inspect_battle_markers(std::ostream&) const;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
