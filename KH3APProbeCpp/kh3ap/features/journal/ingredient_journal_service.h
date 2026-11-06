#pragma once
#include "kh3ap/core/gfx/gfx_reflection_models.h"
#include "kh3ap/core/performance_build.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <cstdint>

namespace RC::Unreal { class UObject; }
namespace kh3ap {
struct RuntimeIngredientJournalConfig {
    GfxProbeFunctionPaths gfx_paths;
    std::function<RC::Unreal::UObject*()> context;
    std::function<std::optional<std::wstring>()> collected;
    std::function<bool()> background_input_allowed;
    std::function<uint64_t()> collected_revision;
};
class RuntimeIngredientJournalService {
public:
    explicit RuntimeIngredientJournalService(RuntimeIngredientJournalConfig);
    ~RuntimeIngredientJournalService();
    auto install_once() -> void;
    auto tick() -> void;
    auto pointer_motion() -> void;
    auto route(int controller, std::wstring_view key, int event, bool gamepad) -> bool;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto diagnostic_json(int navigation = -1) -> std::string;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
