#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
namespace RC::Unreal { class UObject; struct FWeakObjectPtr; }
namespace kh3ap {
struct RuntimeMusicChestConfig {
    std::function<std::optional<std::filesystem::path>()> bridge_dir;
    std::function<RC::Unreal::UObject*()> playable_pawn;
    std::function<bool()> gameplay_ready;
    // Share the verified native activation hook with the map's independent roster.
    std::function<void(const RC::Unreal::FWeakObjectPtr&)> chest_changed;
    std::function<void()> chest_rescan;
};
// Shared by music chest interactions, including the separately gated prototypes.
bool begin_music_chest_pause(RC::Unreal::UObject* pawn);
// Owns native chest discovery, visuals and interaction for Melody and Chestsanity.
// A converted Melody chest takes precedence over a Keyblade requirement.
class RuntimeMusicChestService {
public:
    explicit RuntimeMusicChestService(RuntimeMusicChestConfig);
    ~RuntimeMusicChestService();
    void tick();
    void reset_map_lifecycle();
    void notify_player_changed();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
