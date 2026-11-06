#include "kh3ap/features/colors/color_palette.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    namespace
    {
        constexpr auto palette_hash_initial_state = 1469598103934665603ull;
        constexpr auto palette_hash_prime = 1099511628211ull;
        constexpr auto full_hue_rotation_degrees = 360.0f;
        constexpr auto integer_hue_bucket_count = 360ull;
        constexpr auto hue_sector_degrees = 60.0f;
        constexpr auto green_hue_degrees = 120.0f;
        constexpr auto cyan_hue_degrees = 180.0f;
        constexpr auto blue_hue_degrees = 240.0f;
        constexpr auto magenta_hue_degrees = 300.0f;
        constexpr auto palette_jitter_sample_count = 101ull;
        constexpr auto palette_percentage_scale = 100.0f;
        constexpr auto wild_minimum_saturation = 0.70f;
        constexpr auto wild_saturation_sample_count = 31ull;
        constexpr auto wild_minimum_value = 0.85f;
        constexpr auto wild_value_sample_count = 16ull;
        constexpr auto wild_saturation_hash_shift = 16;
        constexpr auto wild_value_hash_shift = 32;
    }

    // Colours are a pure function of (seed, character, parameter): the
    // same seed reproduces the same party on every load, with no save
    // slot, no generator actor and no map lookup to go wrong. This
    // replaces the ColorRandomData_C/ColorDataGenerator_Actor round trip
    // entirely - that lookup is the one part of ApplyRandomColors never
    // cleared of the fatal, and nothing here can reach it.
    [[nodiscard]] auto hash_color_inputs(
            std::wstring_view seed,
            std::wstring_view character_key,
            std::wstring_view parameter_name) -> uint64_t
    {
        uint64_t hash = palette_hash_initial_state;
        const auto mix = [&hash](std::wstring_view text) {
            for (const wchar_t character : text)
            {
                hash ^= static_cast<uint64_t>(character);
                hash *= palette_hash_prime;
            }
            // Separator, so ("ab","c") and ("a","bc") do not collide.
            hash ^= static_cast<uint64_t>(L'|');
            hash *= palette_hash_prime;
        };
        mix(seed);
        mix(character_key);
        mix(parameter_name);
        return hash;
    }

    // Saturation and value are clamped because the per-character jitter
    // is applied to the caller's constants and can push either past 1.0.
    // Values above 1.0 would brighten rather than tint - these are
    // multipliers onto the character's existing textures - which blows
    // out highlights unpredictably.
    [[nodiscard]] auto hsv_to_rgb(float hue, float saturation, float value) -> DerivedColor
    {
        saturation = std::clamp(saturation, 0.0f, 1.0f);
        value = std::clamp(value, 0.0f, 1.0f);
        const float chroma = value * saturation;
        const float secondary =
                chroma * (1.0f - std::fabs(std::fmod(hue / hue_sector_degrees, 2.0f) - 1.0f));
        const float offset = value - chroma;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if (hue < hue_sector_degrees) { r = chroma; g = secondary; }
        else if (hue < green_hue_degrees) { r = secondary; g = chroma; }
        else if (hue < cyan_hue_degrees) { g = chroma; b = secondary; }
        else if (hue < blue_hue_degrees) { g = secondary; b = chroma; }
        else if (hue < magenta_hue_degrees) { r = secondary; b = chroma; }
        else { r = chroma; b = secondary; }
        return DerivedColor{r + offset, g + offset, b + offset};
    }

    // Rescales a colour so its brightest channel lands on `gain`,
    // keeping the hue and the ratios between channels. Every colour
    // parameter in these shaders is a multiplier, so a value below the
    // peak only darkens what is underneath - on a surface that is
    // already dark, like Toy Sora's hair, that is the difference
    // between a colour and a smudge. Used where a multiplier is the
    // only thing colouring a surface and there is no headroom to waste.
    //
    // This replaced an at_full_value that hardcoded a peak of 1.0.
    // That 1.0 was never the shader's limit, it was ours: these
    // parameters are FVector, not FLinearColor, and nothing clamps
    // them. The shipped assets run well past it on the very parameters
    // this is for - "Metal Base col multi" up to 2.0 (5 of its 19
    // instances above 1.0), "color_mult" 2.8, "BaseColMult" 7.0, the SS
    // colours 3.0, and NunoColorMulti at 5.0 on DisneyOrg_Basemat, the
    // character master. A multiplier capped at 1.0 can only leave one
    // channel alone and darken the other two, which is the definition
    // of a tint; going above it is what repaints. Confirmed in game on
    // the weapons, which read as tinted until the cap came off.
    [[nodiscard]] auto at_gain(const DerivedColor& color, float gain) -> DerivedColor
    {
        const float peak = std::max({color.r, color.g, color.b});
        if (peak <= 0.0f)
        {
            return color;
        }
        const float scale = gain / peak;
        return DerivedColor{color.r * scale, color.g * scale, color.b * scale};
    }

    // The other way to lift a colour past 1.0, and the one the character
    // path needs. at_gain flattens every parameter onto the same peak,
    // which is right where headroom is scarce and wrong here:
    // build_palette deliberately gives each material a different value
    // (nuno 1.00, metal 0.90, leather 0.80) so the zones stay
    // distinguishable from each other. Scaling keeps those differences
    // and just moves the whole set up.
    [[nodiscard]] auto scaled_by(const DerivedColor& color, float gain) -> DerivedColor
    {
        return DerivedColor{color.r * gain, color.g * gain, color.b * gain};
    }

    [[nodiscard]] auto wrap_hue(float hue) -> float
    {
        while (hue < 0.0f) hue += full_hue_rotation_degrees;
        while (hue >= full_hue_rotation_degrees) hue -= full_hue_rotation_degrees;
        return hue;
    }

    // Small deterministic nudge so coordinated characters do not all
    // come out with an identical saturation/value signature.
    [[nodiscard]] auto jitter(uint64_t hash, int shift, float span) -> float
    {
        return (static_cast<float>((hash >> shift) % palette_jitter_sample_count) / palette_percentage_scale - 0.5f) * span;
    }

    [[nodiscard]] auto derive_color(
            std::wstring_view seed,
            std::wstring_view character_key,
            std::wstring_view parameter_name) -> DerivedColor
    {
        const uint64_t hash = hash_color_inputs(seed, character_key, parameter_name);
        const float hue = static_cast<float>(hash % integer_hue_bucket_count);
        // These multiply into the character's existing textures. A
        // multiplier can only darken, never brighten, so value stays
        // high to avoid muddying the result and saturation carries the
        // actual hue shift.
        const float saturation = wild_minimum_saturation + static_cast<float>((hash >> wild_saturation_hash_shift) % wild_saturation_sample_count) / palette_percentage_scale;
        const float value = wild_minimum_value + static_cast<float>((hash >> wild_value_hash_shift) % wild_value_sample_count) / palette_percentage_scale;
        return hsv_to_rgb(hue, saturation, value);
    }

    // Same hue, forced to full saturation and value. For a multiply
    // parameter that is the difference between suggesting a colour and
    // imposing one: at saturation 0.8 every channel of the multiplier is
    // still substantial, so the texture's own hue survives and the
    // result reads as the original slightly shifted (confirmed live on
    // Sora's Monstropolis fur and Goofy's). At saturation 1.0 at least
    // one channel is zero, which forces the hue instead of nudging it.
    // Reserved for the fur/skin multipliers covering a whole creature -
    // on a small trim zone it would just look neon.
    [[nodiscard]] auto derive_color_forced(
            std::wstring_view seed,
            std::wstring_view character_key,
            std::wstring_view parameter_name) -> DerivedColor
    {
        const uint64_t hash = hash_color_inputs(seed, character_key, parameter_name);
        return hsv_to_rgb(static_cast<float>(hash % integer_hue_bucket_count), 0.75f, 1.0f);
    }





    // hue_offset rotates the whole Coordinated palette. Used by the
    // per-slot path to guarantee that two body parts on one
    // character land on different hues: drawing each slot an
    // independent hue does not, and a draw where three of them
    // came out within 10 degrees produced a cowboy who was pink
    // from hat to boots. Wild is left alone - independent draws,
    // collisions included, are what that mode is for.
    [[nodiscard]] auto build_palette(
            std::wstring_view seed,
            std::wstring_view character_key,
            RuntimeColorHarmonyMode mode,
            float hue_offset) -> CharacterPalette
    {
        CharacterPalette palette{};
        if (mode == RuntimeColorHarmonyMode::Wild)
        {
            // Independent draws, hair included: base, tips and
            // subsurface are unrelated by design.
            palette.nuno = derive_color(seed, character_key, L"NunoColorMulti");
            palette.leather = derive_color(seed, character_key, L"LeatherColorMulti");
            palette.metal = derive_color(seed, character_key, L"MetalColorMulti");
            palette.custom = derive_color(seed, character_key, L"CustomColorMulti");
            palette.hair = derive_color(seed, character_key, L"HairColor");
            palette.hair_point_end = derive_color(seed, character_key, L"HairPointEndColor");
            palette.hair_ss = derive_color(seed, character_key, L"HairSSColor");
            palette.base = derive_color(seed, character_key, L"BaseColorMulti");
            palette.paint = derive_color(seed, character_key, L"BodyPaintColor");
            palette.paint2 = derive_color(seed, character_key, L"BodyPaintColor2");
            palette.paint3 = derive_color(seed, character_key, L"BodyPaintColor3");
            palette.base_forced = derive_color_forced(seed, character_key, L"BaseColorMulti");
            palette.base_soft = palette.base_forced;
            return palette;
        }

        // Coordinated: one hue anchors the outfit, a second anchors the
        // hair. Hair gets its own hue because a character whose hair
        // matches their jacket reads as a mistake rather than a design.
        const uint64_t outfit_hash = hash_color_inputs(seed, character_key, L"outfit");
        const uint64_t hair_hash = hash_color_inputs(seed, character_key, L"hair");
        const float outfit_hue = static_cast<float>(outfit_hash % integer_hue_bucket_count) + hue_offset;
        const float hair_hue = static_cast<float>(hair_hash % integer_hue_bucket_count) + hue_offset;

        // Analogous hues around the anchor, with saturation and value
        // per material so the zones stay distinguishable: fabric is the
        // main colour, leather a shade deeper, metal the lightest.
        //
        // Value sits near 1.0 throughout because these are multipliers:
        // anything lower darkens the underlying texture instead of
        // recolouring it, which is why the first pass barely moved the
        // companions. Their base textures are already dark and
        // saturated, so a dim multiplier just muddies them - the hue
        // shift has to come from saturation, at full brightness.
        // Metal was the worst offender at 0.25 saturation: near-white,
        // so it tinted almost nothing.
        palette.nuno = hsv_to_rgb(
                wrap_hue(outfit_hue),
                0.85f + jitter(outfit_hash, 8, 0.12f),
                1.00f + jitter(outfit_hash, 12, 0.06f));
        palette.leather = hsv_to_rgb(
                wrap_hue(outfit_hue - 18.0f),
                0.72f + jitter(outfit_hash, 16, 0.12f),
                0.80f + jitter(outfit_hash, 20, 0.10f));
        palette.metal = hsv_to_rgb(
                wrap_hue(outfit_hue + 18.0f),
                0.50f + jitter(outfit_hash, 24, 0.12f),
                0.90f + jitter(outfit_hash, 28, 0.06f));
        palette.custom = hsv_to_rgb(
                wrap_hue(outfit_hue + 36.0f),
                0.78f + jitter(outfit_hash, 32, 0.12f),
                0.95f + jitter(outfit_hash, 36, 0.08f));

        // Hair as one coherent set: tips brighter and slightly shifted,
        // subsurface more saturated and darker, both anchored to the
        // base hue.
        palette.hair = hsv_to_rgb(
                wrap_hue(hair_hue),
                0.80f + jitter(hair_hash, 8, 0.12f),
                0.95f + jitter(hair_hash, 12, 0.08f));
        palette.hair_point_end = hsv_to_rgb(
                wrap_hue(hair_hue + 14.0f),
                0.85f + jitter(hair_hash, 16, 0.10f),
                1.00f + jitter(hair_hash, 20, 0.06f));
        palette.hair_ss = hsv_to_rgb(
                wrap_hue(hair_hue - 8.0f),
                0.90f + jitter(hair_hash, 24, 0.08f),
                0.85f + jitter(hair_hash, 28, 0.10f));

        // Fur/base tint anchors to the outfit hue too - on a fur-shaded
        // model this is the dominant colour a viewer actually sees, so
        // it belongs with the outfit family rather than off on its own.
        palette.base = hsv_to_rgb(
                wrap_hue(outfit_hue + 8.0f),
                0.80f + jitter(outfit_hash, 40, 0.12f),
                0.95f + jitter(outfit_hash, 44, 0.08f));

        // Three paint zones as one coordinated set around the outfit
        // hue, mirroring how nuno/leather/metal relate to each other -
        // the same "one family, several shades" idea applied to a
        // masked paint-blend shader instead of cloth/leather/metal.
        palette.paint = hsv_to_rgb(
                wrap_hue(outfit_hue - 24.0f),
                0.82f + jitter(outfit_hash, 48, 0.12f),
                0.90f + jitter(outfit_hash, 52, 0.08f));
        palette.paint2 = hsv_to_rgb(
                wrap_hue(outfit_hue + 44.0f),
                0.75f + jitter(outfit_hash, 56, 0.12f),
                0.90f + jitter(outfit_hash, 60, 0.08f));
        const uint64_t paint3_hash = hash_color_inputs(seed, character_key, L"paint3");
        palette.paint3 = hsv_to_rgb(
                wrap_hue(outfit_hue + 96.0f),
                0.70f + jitter(paint3_hash, 8, 0.12f),
                0.90f + jitter(paint3_hash, 12, 0.08f));
        palette.base_forced = hsv_to_rgb(wrap_hue(outfit_hue + 8.0f), 0.75f, 1.0f);
        // 0.38 still read as a full repaint on white surfaces - Donald's
        // feathers and Buzz's suit came out solid red - because a
        // multiply over white shows the multiplier undiluted. The soft
        // path exists for exactly those surfaces, so it has to be much
        // closer to white than it would need to be on dark cloth.
        palette.base_soft = hsv_to_rgb(wrap_hue(outfit_hue + 8.0f), 0.20f, 1.0f);
        return palette;
    }


    [[nodiscard]] auto palette_color(const CharacterPalette& palette, PaletteSlot slot) -> DerivedColor
    {
        switch (slot)
        {
        case PaletteSlot::Nuno: return palette.nuno;
        case PaletteSlot::Leather: return palette.leather;
        case PaletteSlot::Metal: return palette.metal;
        case PaletteSlot::Custom: return palette.custom;
        case PaletteSlot::Hair: return palette.hair;
        case PaletteSlot::HairTips: return palette.hair_point_end;
        case PaletteSlot::HairSS: return palette.hair_ss;
        case PaletteSlot::BaseForced: return palette.base_forced;
        case PaletteSlot::BaseSoft: return palette.base_soft;
        case PaletteSlot::Paint: return palette.paint;
        case PaletteSlot::Paint2: return palette.paint2;
        case PaletteSlot::Paint3: return palette.paint3;
        case PaletteSlot::Base: break;
        }
        return palette.base;
    }
}
