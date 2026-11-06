#pragma once

#include <memory>

namespace kh3ap
{
    // Owns and wires the complete KH3 runtime service graph. UE4SS lifecycle
    // concerns stay in Kh3ApRuntimeMod; feature services stay behind this
    // boundary so their implementation dependencies do not leak into the mod
    // entrypoint.
    class RuntimeComposition final
    {
    public:
        RuntimeComposition();
        ~RuntimeComposition();

        RuntimeComposition(const RuntimeComposition&) = delete;
        auto operator=(const RuntimeComposition&) -> RuntimeComposition& = delete;
        RuntimeComposition(RuntimeComposition&&) = delete;
        auto operator=(RuntimeComposition&&) -> RuntimeComposition& = delete;

        auto initialize() -> void;

        // Re-draws every character's colours from a bumped seed, in place.
        // Bound to a key by the mod shell so colour work can be judged
        // without a relaunch.
        auto reroll_colors() -> void;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
