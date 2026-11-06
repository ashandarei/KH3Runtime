#pragma once

#include "kh3ap/features/colors/color_service.h"

#include <cstdint>
#include <string_view>

namespace kh3ap
{
    // An RGB triple as the material setters want it. Not FLinearColor:
    // these values are written into FVector parameters and routinely
    // exceed 1.0, so nothing here clamps.
    struct DerivedColor
    {
        float r{};
        float g{};
        float b{};
    };

    // Colours are a pure function of (seed, character, parameter), so the
    // same seed reproduces the same party on every load with no save slot
    // and no generator actor involved.
    [[nodiscard]] auto hash_color_inputs(
            std::wstring_view seed,
            std::wstring_view character_key,
            std::wstring_view parameter_name) -> uint64_t;

    [[nodiscard]] auto hsv_to_rgb(float hue, float saturation, float value) -> DerivedColor;

    // Rescales a colour so its brightest channel lands on `gain`, keeping
    // hue and the ratios between channels. `gain` above 1.0 is normal and
    // intended - see the implementation for why 1.0 is not a limit.
    [[nodiscard]] auto at_gain(const DerivedColor& color, float gain) -> DerivedColor;

    // Multiplies every channel by `gain`, keeping the palette's deliberate
    // per-material brightness differences instead of flattening them onto
    // a common peak the way at_gain does.
    [[nodiscard]] auto scaled_by(const DerivedColor& color, float gain) -> DerivedColor;

    [[nodiscard]] auto wrap_hue(float hue) -> float;

    // A small deterministic offset drawn from `hash`, so two characters
    // sharing a base hue still differ in saturation and value.
    [[nodiscard]] auto jitter(uint64_t hash, int shift, float scale) -> float;

    [[nodiscard]] auto derive_color(
            std::wstring_view seed,
            std::wstring_view character_key,
            std::wstring_view parameter_name) -> DerivedColor;

    // derive_color at full saturation and value, for the multipliers that
    // cover an entire surface and have no headroom to spend.
    [[nodiscard]] auto derive_color_forced(
            std::wstring_view seed,
            std::wstring_view character_key,
            std::wstring_view parameter_name) -> DerivedColor;

    // The parameters one character or weapon drives, as one set, so the
    // harmony modes can relate them to each other rather than each call
    // site deriving its own value in isolation.
    struct CharacterPalette
    {
        DerivedColor nuno{};
        DerivedColor leather{};
        DerivedColor metal{};
        DerivedColor custom{};
        DerivedColor hair{};
        DerivedColor hair_point_end{};
        DerivedColor hair_ss{};
        DerivedColor base{};
        DerivedColor paint{};
        DerivedColor paint2{};
        DerivedColor paint3{};
        DerivedColor base_forced{};
        DerivedColor base_soft{};
    };

    // `hue_offset` rotates the whole Coordinated palette, so a caller
    // painting several slots can guarantee they land on different hues.
    [[nodiscard]] auto build_palette(
            std::wstring_view seed,
            std::wstring_view character_key,
            RuntimeColorHarmonyMode mode,
            float hue_offset = 0.0f) -> CharacterPalette;

    // Which entry of a palette a parameter is driven from. Shared by the
    // character and weapon tables: the slots describe roles (fabric,
    // metal, subsurface, ...), not a particular shader's names.
    enum class PaletteSlot : uint8_t
    {
        Nuno, Leather, Metal, Custom,
        Hair, HairTips, HairSS,
        Base, BaseForced, BaseSoft,
        Paint, Paint2, Paint3,
    };

    [[nodiscard]] auto palette_color(const CharacterPalette& palette, PaletteSlot slot) -> DerivedColor;

    // One row of a parameter table: the shader parameter name, the palette
    // entry it is driven from, and whether it wants the colour flattened
    // onto its peak first.
    struct ParameterBinding
    {
        std::wstring_view name;
        PaletteSlot slot;
        bool full_value{false};
    };
}
