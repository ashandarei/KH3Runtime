#pragma once
#include "kh3ap/core/gfx/gfx_reflection_models.h"
#include <functional>
#include <memory>
#include <string>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
// Loads inactive food packages as assets. It never streams/travels a game world,
// runs actor events, grants items, or retains gameplay UObjects between ticks.
class RuntimeIngredientJournalMapService {
public:
    RuntimeIngredientJournalMapService(GfxProbeFunctionPaths,
        std::function<RC::Unreal::UObject*()> asset_context,
        std::function<RC::Unreal::UObject*()> movie_asset);
    ~RuntimeIngredientJournalMapService();
    auto tick(RC::Unreal::UObject* page) -> void;
    auto cancel() -> void;
    auto status() const -> const std::wstring&;
    auto pending() const -> bool;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
