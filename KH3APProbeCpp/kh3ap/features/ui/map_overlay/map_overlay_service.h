#pragma once
#include "kh3ap/core/gfx/gfx_reflection_models.h"
#include "kh3ap/features/ui/map_overlay/lucky_emblem_marker.h"
#include "kh3ap/features/ui/map_overlay/ingredient_marker.h"
#include "kh3ap/features/ui/map_overlay/map_icon_options.h"
#include "kh3ap/core/performance_build.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace RC::Unreal { class UObject; struct FWeakObjectPtr; }
namespace kh3ap {
struct RuntimeMapOverlayConfig {
    GfxProbeFunctionPaths gfx_paths;
    std::function<RC::Unreal::UObject*()> pawn;
    std::function<bool()> gameplay_ready;
    std::function<bool()> enabled;
    std::function<void(RC::Unreal::UObject*,bool rediscover,std::vector<RuntimeLuckyEmblemMarker>&)> lucky_emblems;
    std::function<void(bool rediscover,std::vector<RuntimeIngredientMarker>&)> ingredients;
    std::function<uint32_t()> icon_options;
    std::function<void(MapIconOption,bool)> set_icon_option;
    std::function<bool(bool visible)> set_battle_level_visible;
    std::function<std::optional<bool>(RC::Unreal::UObject*,std::wstring_view)> owns_keyblade;
    std::function<bool()> keybinds_enabled;
};
class RuntimeMapOverlayService {
public:
    explicit RuntimeMapOverlayService(RuntimeMapOverlayConfig);
    ~RuntimeMapOverlayService();
    auto route(int controller, std::wstring_view key, int event, bool gamepad) -> bool;
    auto tick() -> void;
    // Called from the existing game-thread treasure observation drain.
    auto notify_treasure_changed() -> void;
    auto notify_chest_changed(const RC::Unreal::FWeakObjectPtr&) -> void;
    auto reset_map_lifecycle() -> void;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto diagnostic_probe(RC::Unreal::UObject* expected_pawn, bool toggle) -> std::string;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
