#pragma once
#include "kh3ap/features/colors/color_palette.h"
#include <UnrealDef.hpp>
#include <array>

namespace kh3ap::color_service_detail
{
    // Parameter -> palette slot for every character. A name that a given
    // character's materials do not expose costs a no-op, which is what
    // lets one table cover nine unrelated shader families.
    inline constexpr std::array<ParameterBinding, 79> universal_parameter_bindings{{
        {STR("NunoColorMulti"), PaletteSlot::Nuno},
        {STR("LeatherColorMulti"), PaletteSlot::Leather},
        {STR("MetalColorMulti"), PaletteSlot::Metal},
        {STR("CustomColorMulti"), PaletteSlot::Custom},
        {STR("NunoMat"), PaletteSlot::Nuno},
        {STR("LeatherMat"), PaletteSlot::Leather},
        {STR("MetalMat"), PaletteSlot::Metal},
        {STR("CustomMat"), PaletteSlot::Custom},
        {STR("HairColor"), PaletteSlot::Hair},
        {STR("HairPointEndColor"), PaletteSlot::HairTips},
        {STR("HairSSColor"), PaletteSlot::HairSS},
        {STR("HairMat"), PaletteSlot::Hair},
        {STR("BaseColorMulti"), PaletteSlot::BaseForced},
        {STR("SSColorMulti"), PaletteSlot::HairSS},
        {STR("R_SSColorMulti"), PaletteSlot::HairSS},
        {STR("G_SSColorMulti"), PaletteSlot::HairSS},
        {STR("A_SSColorMulti"), PaletteSlot::HairSS},
        {STR("BaseMat"), PaletteSlot::Base},
        {STR("FurBaseMat"), PaletteSlot::Base},
        {STR("ShortFurMat"), PaletteSlot::Base},
        {STR("ShortFurBaseMat"), PaletteSlot::Base},
        {STR("ColorMulti"), PaletteSlot::Base},
        {STR("ShortFurColorMulti"), PaletteSlot::BaseForced},
        {STR("ShortFurSSColorMulti"), PaletteSlot::HairSS},
        {STR("BodyPaintColor"), PaletteSlot::Paint},
        {STR("BodyPaintColor2"), PaletteSlot::Paint2},
        {STR("BodyPaintColor3"), PaletteSlot::Paint3},
        {STR("Mat01"), PaletteSlot::Nuno},
        {STR("Mat02"), PaletteSlot::Leather},
        {STR("Mat03"), PaletteSlot::Metal},
        {STR("Mat04"), PaletteSlot::Custom},
        {STR("Mat05"), PaletteSlot::Base},
        // Woody and Buzz both run Mat01..Mat09, not Mat01..Mat05 -
        // the missing four were invisible only because the dump was
        // truncating at 16 names.
        {STR("Mat06"), PaletteSlot::Paint},
        {STR("Mat07"), PaletteSlot::Paint2},
        {STR("Mat08"), PaletteSlot::Paint3},
        {STR("Mat09"), PaletteSlot::Hair},
        {STR("Mat01_SSColor"), PaletteSlot::HairSS},
        {STR("Mat02_SSColor"), PaletteSlot::HairSS},
        {STR("Mat06_SSColor"), PaletteSlot::HairSS},
        {STR("Mat07_SSColor"), PaletteSlot::HairSS},
        // BaseColor is the only colour parameter n_ts201_H_mat
        // exposes, so gating it to creatures left that surface
        // vanilla for ever. Humans do not appear to expose it at
        // all, which is what made the gate look harmless.
        {STR("BaseColor"), PaletteSlot::Base},
        // Buzz's helmet visor (SQEX_VisorGrass - "visor glass").
        {STR("ViserColor"), PaletteSlot::Metal},
        {STR("ViserCenterColor"), PaletteSlot::Custom},
        // Toy Sora overrides none of his plastic colours, so these
        // follow his mask names (PlasticMaskChannel and the game's
        // own "Prastic" misspelling) on the chance the master
        // material names them to match. No-ops if not.
        {STR("PlasticColor"), PaletteSlot::Nuno},
        {STR("PlasticColorMul"), PaletteSlot::Nuno},
        {STR("PrasticColorMulti"), PaletteSlot::Nuno},
        {STR("SkinPlasticColorMulti"), PaletteSlot::BaseSoft},
        {STR("PlasticColorMulti"), PaletteSlot::Nuno},
        {STR("PlasticMat"), PaletteSlot::Nuno},
        {STR("GlossPlasticColorMulti"), PaletteSlot::Metal},
        {STR("ClearPlasticColorMulti"), PaletteSlot::Custom},
        {STR("ClearPrasticColorMulti"), PaletteSlot::Custom},
        {STR("ClearPrasticMat"), PaletteSlot::Custom},
        {STR("PlasticSSColorMulti"), PaletteSlot::HairSS},
        // --- SQEX_RealChr_BaseMat, the Caribbean "real" shader.
        // Jack Sparrow, Caribbean Sora/Donald/Goofy. Four numbered
        // mask zones instead of named ones, so the zone names carry
        // no hint of what they dress - which is fine, they map onto
        // the four tint slots the same way MatNN does.
        {STR("Mask1_ColorMulti"), PaletteSlot::Nuno, true},
        {STR("Mask2_ColorMulti"), PaletteSlot::Leather, true},
        {STR("Mask3_ColorMulti"), PaletteSlot::Metal, true},
        {STR("Mask4_ColorMulti"), PaletteSlot::Custom, true},
        {STR("Mask1_SSColorMulti"), PaletteSlot::HairSS},
        {STR("Mask2_SSColorMulti"), PaletteSlot::HairSS},
        {STR("Mask3_SSColorMulti"), PaletteSlot::HairSS},
        {STR("Mask4_SSColorMulti"), PaletteSlot::HairSS},
        // --- SQEX_NPC_UseSpeMapBaseMat, the RGBA-channel shader.
        // Rapunzel, Flynn, Baymax. This is the whole reason Rapunzel
        // came out untouched and Flynn came out hair-only: his hair
        // is SQEX_KHHairBaseMat and answers to HairColor, which we
        // already knew, while everything else on both of them is
        // this family and nothing here was in any table.
        {STR("R_Color_Multi"), PaletteSlot::Nuno, true},
        {STR("G_Color_Multi"), PaletteSlot::Leather, true},
        {STR("B_Color_Multi"), PaletteSlot::Metal, true},
        {STR("A_Color_Multi"), PaletteSlot::Custom, true},
        {STR("B_SSColorMulti"), PaletteSlot::HairSS},
        // --- hair and trim shared across both worlds.
        {STR("HairStartColor"), PaletteSlot::HairTips},
        {STR("MaskedAreaColorMulti"), PaletteSlot::Custom},
        {STR("EyelashesColor"), PaletteSlot::Hair},
        // Caribbean Donald's feathers (n_ca001_Feather).
        {STR("SSColor"), PaletteSlot::Base},
        {STR("SSColorD"), PaletteSlot::HairSS},
        {STR("DirtColorMulti"), PaletteSlot::Leather},
        {STR("SubsurfaceColor"), PaletteSlot::HairSS},
        // Face multipliers, softened rather than painted, the same
        // rule the toys established. FaceColMul is the Caribbean
        // spelling, faceMul the NPC one.
        {STR("FaceColMul"), PaletteSlot::BaseSoft},
        {STR("faceMul"), PaletteSlot::BaseSoft},
        // --- n_ra212_HairBaseMat, Rapunzel's hair. It lives on a
        // SECOND mesh component (TresCharHairMesh1), which is why
        // colouring her body left it untouched - not a missing
        // component, just a master nothing had a name for.
        // BaseHairColor is a genuine base colour rather than a
        // multiplier, so this one really does repaint.
        // The HairEmissiv* pair is deliberately left out: it is
        // what makes her hair glow, and driving it would light her
        // up permanently.
        {STR("BaseHairColor"), PaletteSlot::Hair},
        {STR("DeepHairColor"), PaletteSlot::HairSS},
        {STR("SSHairColor"), PaletteSlot::HairSS},
    }};

    // Skin, and the one parameter that replaces a diffuse outright.
    inline constexpr std::array<ParameterBinding, 10> creature_parameter_bindings{{
        // Declared by p_ts001_Basemat and present in neither table
        // before - the subsurface half of a toy's skin.
        {STR("SkinSSColorMulti"), PaletteSlot::HairSS},
        {STR("SkinColor"), PaletteSlot::Base},
        {STR("FurSkinColor"), PaletteSlot::Base},
        {STR("SkinMat"), PaletteSlot::Base},
        {STR("SkinColorMulti"), PaletteSlot::BaseForced},
        {STR("SkinMatMulti"), PaletteSlot::BaseForced},
        {STR("Skin_SubSurf_Color"), PaletteSlot::HairSS},
        {STR("SkinSSMulti"), PaletteSlot::HairSS},
        // Lower-case s, and distinct from SkinColorMulti above -
        // the Caribbean and NPC shaders spell it this way. Baymax
        // is a creature and is white, so his needs forcing.
        {STR("skinColorMulti"), PaletteSlot::BaseForced},
        {STR("SkinSSOutColorMulti"), PaletteSlot::HairSS},
    }};

    // DisneyOrg uses SkinColorMul, distinct from the creature table's
    // SkinColorMulti. Mask selectors are intentionally excluded: their
    // vectors control coverage rather than color.
    inline constexpr std::array<ParameterBinding, 16> disney_parameter_bindings{{
        {STR("FeatherBaseMat"), PaletteSlot::Base},
        {STR("FeatherColorMul"), PaletteSlot::BaseForced},
        {STR("FeatherDeepMatMul"), PaletteSlot::Leather},
        {STR("FeatherSSMulti"), PaletteSlot::HairSS},
        {STR("FeatherOutSSMulti"), PaletteSlot::HairSS},
        {STR("SkinColorMul"), PaletteSlot::Base},
        {STR("SkinBaseMat"), PaletteSlot::Base},
        {STR("SkinDeepMatMul"), PaletteSlot::Leather},
        {STR("DeepskinColorMulti"), PaletteSlot::Leather},
        {STR("ShortFurColorMulti"), PaletteSlot::BaseForced},
        {STR("MouthMat"), PaletteSlot::Paint2},
        {STR("MouthSSMulti"), PaletteSlot::HairSS},
        // Eye whites and Goofy's nose: softened rather than painted,
        // because a saturated eye white reads as an injury.
        {STR("EyeWhiteMat"), PaletteSlot::BaseSoft},
        {STR("EyeWhiteSSMulti"), PaletteSlot::BaseSoft},
        {STR("EyeWhiteNoseMat"), PaletteSlot::BaseSoft},
        {STR("EyeWhiteNoseColorMulti"), PaletteSlot::BaseSoft},
    }};

    // The Links' own names, written only on a Link. Read off each
    // Link's material manifest (the colour discovery probe).
    // Stitch runs SQEX_NPC_Basemat_MNLS, whose deep-skin and outer
    // subsurface are in no other table; Simba runs his own master,
    // o_ex100_mat0 - FireBody2 is his body, Tategami his mane, the
    // InLight pair the glow inside him. EyeEmissiv is left out: it
    // lights his eyes. Ralph needs nothing here - the universal table
    // already names his hair and clothes. Ariel's projectiles are
    // separate effects and keep their colour.
    inline constexpr std::array<ParameterBinding, 9> link_parameter_bindings{{
        {STR("DeepskinColorMulti"), PaletteSlot::Leather},
        {STR("SkinOutSSMulti"), PaletteSlot::HairSS},
        {STR("FireBody2Color"), PaletteSlot::Base},
        {STR("TategamiColor"), PaletteSlot::Hair},
        {STR("MultiColor"), PaletteSlot::Nuno},
        {STR("InLightColor"), PaletteSlot::HairSS},
        {STR("InLightOutColor"), PaletteSlot::HairTips},
        // Ariel's WaterBaseMat. Neither is overridden by her
        // instances, so only the master's own parameter list
        // (log_master_parameters) showed them: both ship at the
        // same blue, (0.17, 0.46, 1.0).
        {STR("WaterColor"), PaletteSlot::Base},
        {STR("DarkWaterColor"), PaletteSlot::Leather},
    }};

    // SQEX_IrisTexEyeMaterial only. Written just for a slot flagged as
    // eyes, never across a character: "Color" is generic enough that a
    // blanket write would reach materials it was never meant to.
    inline constexpr std::array<ParameterBinding, 5> eye_parameter_bindings{{
        {STR("Color"), PaletteSlot::Base},
        {STR("Color_Dark"), PaletteSlot::Leather},
        {STR("EyeEmissivColor"), PaletteSlot::HairSS},
        // SQEX_WhiteEyeMaterial, a separate slot from the iris on
        // the Caribbean and Corona casts.
        {STR("WhiteEyeColor"), PaletteSlot::Base},
        {STR("EyeirisColor"), PaletteSlot::Custom},
    }};
} // namespace kh3ap::color_service_detail
