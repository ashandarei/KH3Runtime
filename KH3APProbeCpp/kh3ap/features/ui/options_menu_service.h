#pragma once

#include "kh3ap/core/gfx/gfx_reflection_models.h"
#include "kh3ap/features/ui/options_menu.h"

#include <memory>
#include <vector>

namespace kh3ap
{
    struct RuntimeOptionsMenuConfig
    {
        std::vector<RuntimeOptionsMenuDefinition> menus{};
        GfxProbeFunctionPaths gfx_paths{};
        bool enabled{};
        std::function<bool()> background_controller_input_allowed{};
        std::function<bool(int32_t, std::wstring_view, int32_t, bool)> gameplay_key{};
        std::function<void()> pointer_motion{};
    };

    // Renders declarative option trees in KH3's stock Config Scaleform movie.
    // The host owns navigation and presentation; definitions own all state.
    class RuntimeOptionsMenuService
    {
    public:
        explicit RuntimeOptionsMenuService(RuntimeOptionsMenuConfig config);
        ~RuntimeOptionsMenuService();

        RuntimeOptionsMenuService(const RuntimeOptionsMenuService&) = delete;
        auto operator=(const RuntimeOptionsMenuService&)
                -> RuntimeOptionsMenuService& = delete;
        RuntimeOptionsMenuService(RuntimeOptionsMenuService&&) noexcept;
        auto operator=(RuntimeOptionsMenuService&&) noexcept
                -> RuntimeOptionsMenuService&;

        auto install_once() -> void;
        auto tick() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
