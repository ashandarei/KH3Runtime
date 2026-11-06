#pragma once

#include "kh3ap/features/colors/color_service.h"

#include <chrono>
#include <memory>
#include <string_view>

namespace kh3ap
{
    // Colouring for the Gummi Ship, kept apart from the characters like the
    // weapons are.
    //
    // A built ship is an ATresCreateGummiShipActor: one skeletal mesh per
    // block in the departure menu, a handful of merged meshes in flight, and
    // every block material a dynamic instance the game has already filled
    // with the ship's colours (four PTN1_* pattern colours and
    // CT_CustomColor). Painting remaps each of those colours to one drawn
    // from the seed, so blocks that shared a colour still share one and the
    // ship's design stays readable.
    //
    // Only prefab ships are painted. A ship counts as a prefab when its
    // blocks match one of the game's plan tables (TresGummiShipPlanData)
    // exactly; a ship the player built, or a prefab they edited, keeps the
    // colours they gave it. The editor's own ship is never touched, so what
    // the player paints there is what they see.
    //
    // Works only on the world map and the Gummi routes; everywhere else
    // tick() is one cached comparison.
    class RuntimeColorGummi
    {
    public:
        RuntimeColorGummi();
        ~RuntimeColorGummi();

        RuntimeColorGummi(const RuntimeColorGummi&) = delete;
        auto operator=(const RuntimeColorGummi&) -> RuntimeColorGummi& = delete;

        // Discovers ships from lifecycle events and paints bounded material slots per
        // call, so a ship of 300 blocks is spread over several frames.
        auto tick(
                std::wstring_view seed,
                RuntimeColorHarmonyMode mode,
                std::chrono::steady_clock::time_point now) -> void;

        // Writes the game's own colours back on every painted ship, and
        // returns how many slots were restored.
        auto restore_all() -> int;

        // Paints every known ship again on the next ticks - after a re-roll
        // or a mode change.
        auto repaint_all() -> void;

        auto on_map_jump() -> void;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        [[nodiscard]] auto test_snapshot() const -> std::string;
#endif

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
