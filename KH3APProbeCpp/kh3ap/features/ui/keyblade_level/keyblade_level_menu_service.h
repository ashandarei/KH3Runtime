#pragma once

#include "kh3ap/core/gfx/gfx_reflection_models.h"

#include <functional>
#include <memory>

namespace kh3ap
{
    struct RuntimeKeybladeLevelMenuConfig
    {
        GfxProbeFunctionPaths gfx_paths{};
        std::function<bool()> levels_start_at_max{};
        bool enabled{};
    };

    // Replaces the stock Camp Menu equipment-selection instruction with the
    // selected Keyblade's level while the weapon replacement list has focus.
    class RuntimeKeybladeLevelMenuService
    {
    public:
        explicit RuntimeKeybladeLevelMenuService(
                RuntimeKeybladeLevelMenuConfig config);
        ~RuntimeKeybladeLevelMenuService();

        RuntimeKeybladeLevelMenuService(
                const RuntimeKeybladeLevelMenuService&) = delete;
        auto operator=(const RuntimeKeybladeLevelMenuService&)
                -> RuntimeKeybladeLevelMenuService& = delete;
        RuntimeKeybladeLevelMenuService(
                RuntimeKeybladeLevelMenuService&&) noexcept;
        auto operator=(RuntimeKeybladeLevelMenuService&&) noexcept
                -> RuntimeKeybladeLevelMenuService&;

        auto install_once() -> void;
        auto tick() -> void;
        auto observe_progressive_keyblade_level_applied() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
