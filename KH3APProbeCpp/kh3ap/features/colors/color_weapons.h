#pragma once

#include "kh3ap/features/colors/color_service.h"

#include <string_view>
#include <utility>

namespace kh3ap
{
    class RuntimeColorApplyState;

    // Colouring for the things a character carries, kept apart from the
    // character colouring entirely.
    //
    // The split is not cosmetic. A weapon is a separate actor - KH3's
    // ATresWeaponBase derives from ATresPawnBase, not from any component -
    // reached through the pawn's equipment component, and it has a lifetime
    // of its own: summoned, dismissed, swapped from the menu and replaced
    // outright by every formchange. So it is found differently, painted on
    // a different schedule, and answers to a different family of shader
    // parameter names than anything on a character. The only things the two
    // share are the palette and the material calls, which is what
    // runtime_color_palette and runtime_color_material_ops are for.

    // Paints every weapon held by a pawn that has already been coloured.
    //
    // Pawns are read from `state`, and weapons painted here are marked on
    // it, so each weapon is painted once - and a keyblade swapped in later
    // is simply one that has not been marked yet. A weapon whose mesh is
    // not render-ready is left unmarked. Returns false only for unfinished
    // work, allowing a bounded retry cadence that stops when it is ready.
    //
    // Callers own the throttling and the safety gating; this does neither.
    auto runtime_color_apply_equipped_weapons(
            RuntimeColorApplyState& state,
            std::wstring_view seed,
            RuntimeColorHarmonyMode mode) -> bool;

    // Puts every weapon coloured since the last reset back to the materials
    // it shipped with, and reports {weapons, slots} actually restored.
    //
    // Walking the coloured pawns does not reach these: a weapon is not a
    // component of the pawn holding it. A keyblade left painted after the
    // setting is switched off reads as the switch not working, just as
    // loudly as a painted Sora does.
    [[nodiscard]] auto runtime_color_restore_weapons(
            const RuntimeColorApplyState& state) -> std::pair<int, int>;
}
