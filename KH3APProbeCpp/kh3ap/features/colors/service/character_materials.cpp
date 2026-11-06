#include <optional>
#include "kh3ap/app/config.h"
#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/colors/service/character_parameters.h"
#include "kh3ap/features/colors/color_material_ops.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace kh3ap::color_service_detail
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto subsurface_color_gain = 1.3f;
        constexpr auto skin_color_gain = 1.4f;
        constexpr auto outfit_color_gain = 1.5f;
        constexpr auto max_character_material_slots = 24;
        constexpr auto character_color_channel_max = 255.0f;
        using ::kh3ap::settings::s_debug_logging;
    } // namespace

    // How far past 1.0 each palette slot is driven on a character.
    //
    // Same finding as the weapons: hsv_to_rgb clamps value to 1.0, so
    // every colour the palette draws is capped by construction - and a
    // multiplier capped at 1.0 can only leave one channel alone and
    // darken the other two, which is a tint. The comment on
    // build_palette describes exactly this ("a dim multiplier just
    // muddies them - the hue shift has to come from saturation, at full
    // brightness"); driving hue from saturation was the right answer
    // inside a 1.0 box, but the box is not real. The character shader
    // takes more: DisneyOrg_Basemat, the master this table's
    // Nuno/Leather/Metal/Custom names belong to, is shipped with
    // NunoColorMulti at 5.0 on o_mi214.
    //
    // Unlike the weapons, this is NOT uniform, and the reason is
    // already in this file: a multiply over a white surface shows the
    // multiplier undiluted, which is why base_soft exists and why
    // Donald's feathers and Buzz's suit came out solid red at 0.38.
    // So the one slot that must never be lifted is the soft path -
    // it is what Coordinated writes onto feathers, and what
    // soften_head_materials writes onto every face.
    //
    // The gains are deliberately more modest than the weapons' 2.0:
    // there, per-parameter shipped values said how much headroom each
    // one had. Here the only evidence is that the shader accepts more,
    // not how much suits a given character - so this is a starting
    // point to iterate on with F9, not a measured figure.
    [[nodiscard]] auto character_slot_gain(PaletteSlot slot) -> float
    {
        switch (slot)
        {
        // The white-surface and face path. Never lifted.
        case PaletteSlot::BaseSoft:
            return 1.0f;
        // Subsurface is a glow through the surface; it turns waxy
        // before the surface colours themselves are done moving.
        case PaletteSlot::HairSS:
            return subsurface_color_gain;
        // Fur and skin already draw at full value, and cover the bulk
        // of a creature - a smaller step goes further here.
        case PaletteSlot::Base:
        case PaletteSlot::BaseForced:
            return skin_color_gain;
        default:
            break;
        }
        // Outfit zones and hair: dark, saturated base textures, which
        // is where the muddying was worst.
        return outfit_color_gain;
    }

    // The palette colour for a slot, lifted by that slot's gain.
    // `normalize` is the existing full_value behaviour - flatten onto
    // the peak first, for the families with no headroom to spend -
    // carried up to the gain instead of to 1.0.
    [[nodiscard]] auto character_color_for_slot(const CharacterPalette& palette, PaletteSlot slot,
                                                bool normalize) -> DerivedColor
    {
        const DerivedColor color = palette_color(palette, slot);
        const float gain = character_slot_gain(slot);
        if (normalize)
        {
            return at_gain(color, gain);
        }
        return gain == 1.0f ? color : scaled_by(color, gain);
    }

    namespace
    {
        // The colours are set by name across every material, which is what
        // makes one table cover nine shader families - but it also means a
        // parameter that dresses an outfit on one slot paints a face on
        // another. Setting a value on a specific slot's dynamic instance
        // wins over the by-name pass, so this runs afterwards and walks the
        // face back down to the soft tint, leaving the outfit at full
        // strength. That is the separation that no amount of parameter
        // naming could give.
        auto soften_head_materials(UObject* component, const CharacterPalette& palette,
                                   std::wstring_view character_key) -> void
        {
            constexpr std::array<std::wstring_view, 15> head_surface_parameters{
                STR("PlasticColorMulti"),
                STR("PlasticMat"),
                STR("PlasticSSColorMulti"),
                STR("Mat05"),
                STR("BaseColor"),
                STR("BaseColorMulti"),
                STR("ColorMulti"),
                STR("SkinColor"),
                STR("SkinColorMulti"),
                STR("NunoColorMulti"),
                // The lower-case spelling and the face multipliers from the
                // Caribbean and NPC shaders. Without these the softening
                // pass walks straight past a face material it has correctly
                // identified - which is exactly what happened to Pooh Sora.
                STR("skinColorMulti"),
                STR("SkinMatMulti"),
                STR("SkinSSOutColorMulti"),
                STR("faceMul"),
                STR("FaceColMul"),
            };
            const std::optional<int> count = read_num_materials(component);
            const int limit = std::min(count.value_or(0), max_character_material_slots);
            for (int index = 0; index < limit; ++index)
            {
                UObject* const material = read_material_at(component, index);
                UObject* const parent = read_strong_object_property(material, STR("Parent"));
                // The parent asset only, never the dynamic instance: the
                // game writes atama_mtr_x / atama_WP (head orientation) into
                // the runtime instances of almost every slot, so testing
                // those matched all six and softened the whole character.
                // The baked overrides on the asset are what actually mark a
                // face material.
                if (!material_is_head(parent))
                {
                    continue;
                }
                UObject* const instance = create_dynamic_material_instance(component, index);
                if (!instance)
                {
                    continue;
                }
                for (const std::wstring_view parameter : head_surface_parameters)
                {
                    set_vector_parameter_on_mid(instance, parameter, palette.base_soft,
                                                character_key);
                }
                if (s_debug_logging)
                {
                    Output::send<LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Color head material softened key={} slot={}\n"),
                        character_key, index);
                }
            }
        }

    } // namespace

    auto apply_character_colors(UObject* component, std::wstring_view character_key,
                                std::wstring_view seed, RuntimeColorHarmonyMode mode) -> void
    {
        const CharacterPalette palette = build_palette(seed, character_key, mode);
        const bool wild = (mode == RuntimeColorHarmonyMode::Wild);
        const bool creature = character_is_creature(character_key);

        // Every write on this path goes out at its slot's gain, so the
        // feather family below keeps following the mode exactly as it
        // did: Coordinated resolves it to BaseSoft (gain 1.0, Donald
        // stays a white duck) and Wild to BaseForced.
        const auto color_for = [&](std::wstring_view parameter, PaletteSlot slot) -> DerivedColor
        {
            return character_color_for_slot(palette, slot, false);
        };
        const auto paint = [&](std::wstring_view parameter, PaletteSlot slot)
        {
            set_vector_parameter_on_materials(component, parameter, color_for(parameter, slot),
                                              character_key);
        };
        const auto paint_binding = [&](const ParameterBinding& binding)
        {
            set_vector_parameter_on_materials(
                component, binding.name,
                character_color_for_slot(palette, binding.slot, binding.full_value), character_key);
        };

        // Characters with an identified slot plan are coloured slot by
        // slot and skip the by-name pass entirely. The by-name pass
        // cannot tell one slot from another, so running it as well would
        // paint the very surfaces the plan deliberately leaves alone.
        const std::wstring world_name = resolve_world_name(component);
        if (character_has_slot_plan(character_key, world_name))
        {
            apply_slot_plan_colors(component, character_key, world_name, seed, mode);
            return;
        }

        // Toy Box characters without a slot plan still opt out of the
        // by-name pass outside Wild: their zone masks overlap, so no
        // subset of names colours a toy without painting its face.
        if (!wild && character_is_toy(character_key))
        {
            return;
        }

        // The graph's own opening move: three hair parameters on the
        // dynamic instance at ElementIndex=1. Kept because it is what the
        // original did, and harmless where slot 1 is not the hair
        // material - the same three are set by name below, which is what
        // actually reaches the hair wherever it lives.
        if (UObject* const material_instance = create_dynamic_material_instance(component, 1))
        {
            set_vector_parameter_on_mid(material_instance, STR("HairColor"),
                                        color_for(STR("HairColor"), PaletteSlot::Hair),
                                        character_key);
            set_vector_parameter_on_mid(material_instance, STR("HairPointEndColor"),
                                        color_for(STR("HairPointEndColor"), PaletteSlot::HairTips),
                                        character_key);
            set_vector_parameter_on_mid(material_instance, STR("HairSSColor"),
                                        color_for(STR("HairSSColor"), PaletteSlot::HairSS),
                                        character_key);
        }

        for (const ParameterBinding& binding : universal_parameter_bindings)
        {
            // Handled below - skipped entirely on a toy.
            if (binding.name == STR("Mat05") || binding.name == STR("Mat07") ||
                binding.name == STR("BaseColor") || binding.name == STR("PlasticColorMulti"))
            {
                continue;
            }
            paint_binding(binding);
        }

        if (character_is_link(character_key))
        {
            for (const ParameterBinding& binding : link_parameter_bindings)
            {
                paint_binding(binding);
            }
            // Stitch and Meow Wow run Donald and Goofy's shader family.
            // Only the multipliers are written: the *Mat entries of that
            // table are packed surface settings (SkinBaseMat ships at
            // (0, 0.2, 0.7)), not colours.
            //
            // Neither body can be recoloured. Both ship SkinMaskChannel
            // at (0,0,0), so the skin zone covers nothing, and pointing
            // it at each channel of the mask in turn changed nothing
            // either (tested live on both): the body is the base
            // texture itself. What does take is their outline glow
            // (the subsurface colours) and Stitch's blasters (Nuno).
            for (const ParameterBinding& binding : disney_parameter_bindings)
            {
                if (!binding.name.ends_with(STR("Mat")))
                {
                    paint_binding(binding);
                }
            }
        }

        // Skin is the costume on a monster and a rash on a person - see
        // creature_character_keys. Wild ignores the distinction: it
        // repaints everything, and a character excluded from that list
        // would otherwise keep vanilla skin in the one mode that is
        // supposed to spare nothing. Pooh-world Sora is exactly that
        // case - removed from the list so Coordinated leaves his face
        // alone, which also silently opted him out of Wild.
        if (creature || wild)
        {
            for (const ParameterBinding& binding : creature_parameter_bindings)
            {
                paint_binding(binding);
            }
        }

        // Donald's feathers are white, and white multiplied by a
        // saturated colour IS that colour - so every feather parameter,
        // not just the base one, has to follow the mode. Coordinated
        // keeps him a white duck; Wild repaints him.
        paint(STR("FeatherColorMul"), wild ? PaletteSlot::BaseForced : PaletteSlot::BaseSoft);
        paint(STR("FeatherDeepMatMul"), wild ? PaletteSlot::Leather : PaletteSlot::BaseSoft);
        paint(STR("FeatherSSMulti"), wild ? PaletteSlot::HairSS : PaletteSlot::BaseSoft);
        paint(STR("FeatherOutSSMulti"), wild ? PaletteSlot::HairSS : PaletteSlot::BaseSoft);
        if (wild)
        {
            paint(STR("FeatherBaseMat"), PaletteSlot::Base);
        }

        // These four are skipped by the loop above so they can be
        // mode-gated here: full strength in Wild, a soft tint otherwise.
        //
        // No current character reaches this. Every key in
        // toy_character_keys now has a slot plan and returns before this
        // point, and these names are exposed almost only by toys. It is
        // kept as the fallback for a toy-shaded character added without
        // a plan, where painting a dominant zone at full strength reads
        // as a paint dip rather than a costume change.
        //
        // The zone-to-body-part attributions this block used to carry
        // came from the per-zone probe, which writes every name in array
        // order so only the last match is visible. The slot plans
        // supersede them; COLOR_RANDOMIZATION_NOTES.md has the detail.
        {
            paint(STR("Mat05"), wild ? PaletteSlot::Base : PaletteSlot::BaseSoft);
            paint(STR("Mat07"), wild ? PaletteSlot::Paint2 : PaletteSlot::BaseSoft);
            paint(STR("BaseColor"), wild ? PaletteSlot::Base : PaletteSlot::BaseSoft);
            paint(STR("PlasticColorMulti"), wild ? PaletteSlot::Nuno : PaletteSlot::BaseSoft);
        }

        // Last, because setting a value on a specific slot's dynamic
        // instance overrides the by-name pass above.
        //
        // Skipped in Wild: this pass exists to walk a face back down
        // to a soft tint, which is the opposite of what Wild is for.
        if (!wild)
        {
            soften_head_materials(component, palette, character_key);
        }

        // The colours themselves, so "what did this seed actually put on
        // this character" is answerable from the log instead of by
        // re-deriving the hash by hand.
        const auto hex = [](const DerivedColor& color)
        {
            const auto channel = [](float value)
            {
                return static_cast<int>(
                    std::lround(std::clamp(value, 0.0f, 1.0f) * character_color_channel_max));
            };
            return std::format(STR("#{:02X}{:02X}{:02X}"), channel(color.r), channel(color.g),
                               channel(color.b));
        };
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Color applied key={} seed={} mode={} nuno={} leather={} metal={} ")
                STR("custom={} hair={} hair_tips={} hair_ss={} base={} base_forced={} paint={} "
                    "paint2={} paint3={}\n"),
            character_key, seed.empty() ? std::wstring_view{STR("<none>")} : seed,
            mode == RuntimeColorHarmonyMode::Wild ? std::wstring_view{STR("wild")}
                                                  : std::wstring_view{STR("coordinated")},
            hex(palette.nuno), hex(palette.leather), hex(palette.metal), hex(palette.custom),
            hex(palette.hair), hex(palette.hair_point_end), hex(palette.hair_ss), hex(palette.base),
            hex(palette.base_forced), hex(palette.paint), hex(palette.paint2), hex(palette.paint3));
    }
} // namespace kh3ap::color_service_detail
