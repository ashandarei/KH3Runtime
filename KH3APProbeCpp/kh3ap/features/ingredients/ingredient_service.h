#pragma once
#include "kh3ap/features/ui/map_overlay/ingredient_marker.h"
#include "kh3ap/core/performance_build.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
// Shared constructor observer; queues only exact authored ingredient names.
auto notify_ingredient_marker_created(RC::Unreal::UObject*) -> void;
struct RuntimeIngredientConfig {
    std::function<std::optional<std::filesystem::path>()> bridge_dir;
    // Current owned player's world context for read-only inventory snapshots.
    std::function<RC::Unreal::UObject*()> inventory_context;
};
class RuntimeIngredientService {
public:
    explicit RuntimeIngredientService(RuntimeIngredientConfig);
    ~RuntimeIngredientService();
    void install_once();
    // Cheap completion mailbox check; idle frames do no checkpoint I/O.
    void tick();
    void shutdown();
    auto uncollected_markers(bool rediscover,std::vector<RuntimeIngredientMarker>& markers) -> void;
    // Recorded first field acquisitions, independent of current stock or AP mode.
    auto journal_collected() -> std::optional<std::wstring>;
    auto journal_revision() const -> uint64_t;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    // Read-only, bounded stock-query benchmark on the existing game thread.
    static auto benchmark_stock(RC::Unreal::UObject* expected_pawn) -> std::string;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
