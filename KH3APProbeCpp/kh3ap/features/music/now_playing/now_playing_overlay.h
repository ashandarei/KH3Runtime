#pragma once
#include "kh3ap/core/gfx/gfx_reflection_models.h"
#include <functional>
#include <memory>
#include <string>
namespace kh3ap {
struct NowPlayingOverlayConfig {
    GfxProbeFunctionPaths gfx;
    std::function<const std::wstring&()> title;
    std::function<bool()> visible;
};
class NowPlayingOverlay {
public:
    explicit NowPlayingOverlay(NowPlayingOverlayConfig);
    ~NowPlayingOverlay();
    auto tick() -> void;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto test_snapshot() -> std::string;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
