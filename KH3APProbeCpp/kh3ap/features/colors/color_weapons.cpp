#include <utility>
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/colors/color_weapons.h"

#include "kh3ap/features/colors/color_apply_state.h"
#include "kh3ap/features/colors/color_material_ops.h"
#include "kh3ap/features/colors/color_palette.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto max_weapon_material_slots = 16;
        constexpr auto weapon_color_channel_max = 255.0f;
        using ::kh3ap::settings::s_debug_logging;

        // --- Weapons ---------------------------------------------------
        //
        // A weapon is not part of the character wearing it. ATresWeaponBase
        // derives from ATresPawnBase (so, an Actor with its own mesh
        // component), and the pawn reaches it through
        // ATresCharPawnBase::MyEquipment -> UTresEquipmentComponent
        // ::m_WeaponEquips, a TArray<ATresWeaponBase*>. That is why the
        // mesh scan never saw one: K2_GetComponentsByClass on Sora's pawn
        // reports a single component (enumerated=1 in the live log), the
        // body - the keyblade is a sibling actor, not a component.
        //
        // Both property names come from the TresGame headers
        // (TresCharPawnBase.h, TresEquipmentComponent.h): reflected
        // UPROPERTYs, so a plain property read reaches them with no
        // ProcessEvent and no Blueprint involved.
        constexpr std::wstring_view equipment_component_property_name = STR("MyEquipment");
        constexpr std::wstring_view weapon_equips_property_name = STR("m_WeaponEquips");
        constexpr std::wstring_view actor_class_path = STR("/Script/Engine.Actor");

        [[nodiscard]] auto resolve_equipped_weapons(UObject* pawn) -> std::vector<UObject*>
        {
            if (!pawn || !is_uobject_valid_fast(pawn))
            {
                return {};
            }
            UObject* const equipment =
                read_strong_object_property(pawn, equipment_component_property_name.data());
            if (!equipment)
            {
                return {};
            }
            std::vector<UObject*> weapons =
                read_object_array_property(equipment, weapon_equips_property_name.data());
            // Everything downstream treats a weapon as an actor - both
            // component lookups take one - so the array's declared element
            // type is verified rather than trusted. TresEquipmentComponent
            // declares TArray<ATresWeaponBase*> and ATresWeaponBase is an
            // APawn, but this array is Transient and populated at runtime,
            // and handing ProcessEvent a non-actor would not fail quietly.
            UClass* const actor_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, actor_class_path.data());
            if (!actor_class || !is_uobject_valid_fast(actor_class))
            {
                return {};
            }
            std::erase_if(weapons,
                          [actor_class](UObject* weapon)
                          {
                              UClass* const klass = weapon ? weapon->GetClassPrivate() : nullptr;
                              return !klass || !is_uobject_valid_fast(klass) ||
                                     !klass->IsChildOf(actor_class);
                          });
            return weapons;
        }

        // The palette key for a weapon is the weapon's own class name, not
        // its wielder's. Two consequences, both wanted: one keyblade keeps
        // the same colours for a whole seed no matter who is holding it or
        // how often it is dismissed and re-summoned, and swapping keyblade
        // genuinely changes the colours rather than repainting the new one
        // to match the old.
        [[nodiscard]] auto resolve_weapon_key(UObject* weapon) -> std::wstring
        {
            if (!weapon || !is_uobject_valid_fast(weapon))
            {
                return std::wstring{};
            }
            UClass* const klass = weapon->GetClassPrivate();
            if (!klass || !is_uobject_valid_fast(klass))
            {
                return std::wstring{};
            }
            std::wstring name = klass->GetName();
            // Blueprint classes carry a _C suffix; the asset name underneath
            // it is the stable identity (w_so010 and w_so010_C must not draw
            // two different palettes if the pawn is ever re-derived).
            if (name.ends_with(STR("_C")))
            {
                name.resize(name.size() - std::wstring_view{L"_C"}.size());
            }
            return name;
        }

        // Parameter -> palette slot for weapon materials. A separate table
        // from the character one because the weapon shaders are a separate
        // family: the shared master (w_allBaseMat / w_Sora_BaseMat, the
        // parent of every plain keyblade instance) splits a weapon into
        // named material zones - Metal, Leather, Cloth, Plastic, Wood, Gem,
        // Crystal - each with a "<zone> Mask", a "<zone> Setting" and a
        // "<zone> Base col multi". Names with spaces in them, which is why
        // nothing in the character table ever matched.
        //
        // Every name here was read out of the shipped material assets
        // (Content/Character/wep/**), and only names whose shipped default
        // is an actual colour at or below 1.0 are listed. Three families
        // are deliberately absent, and must stay absent:
        //
        //   "<zone> Setting", *_Settings, *Param, *_param, MSR - these pack
        //   metallic/specular/roughness into a vector. Writing a colour
        //   into one turns the surface to chrome; the per-zone probe has
        //   already demonstrated exactly that on Woody and Buzz.
        //
        //   *Mask*, *MaskChannel* - channel selectors that decide which
        //   texture channel drives a zone. Writing a colour re-assigns the
        //   zones themselves.
        //
        //   *_XAxis, *_YAxis, *_ZAxis, *wTrans, *_trans, center_pos,
        //   *tiling_offset, CrystalUV, Wave01/02, NormalOffset - transform,
        //   UV and joint-deform vectors. w_so172 is almost entirely these.
        //   Writing a colour into them deforms the model.
        //
        // Also excluded, for a different reason: AuraColor / AuraOutColor /
        // InLightColor / InLightOutColor (on nearly every Donald weapon and
        // Sora's w_so02x) ship black and are what the game drives to light a
        // weapon up during a command. A colour written there could leave the
        // weapon permanently glowing, so they are left to the game. Same for
        // the HDR emissives whose shipped values run to 2-15 (Bubble_Col,
        // Light_Col, EmissivColor01/02, top_fire_col, lineConvexCol,
        // IceOutColor, light0..3_col): clamping those to a <=1 colour would
        // put a glow out rather than recolour it.
        constexpr std::array<ParameterBinding, 100> weapon_parameter_bindings{{
            // --- the shared weapon master's zones. These alone cover
            // every plain keyblade, staff and shield, Kingdom Key
            // included (w_so010 overrides Metal Base col multi and
            // nothing else).
            {STR("Metal Base col multi"), PaletteSlot::Metal, true},
            {STR("Leather Base col multi"), PaletteSlot::Leather, true},
            {STR("Plastic Base col multi"), PaletteSlot::Nuno, true},
            {STR("Leather SS Col"), PaletteSlot::HairSS},
            {STR("Cloth SS Col"), PaletteSlot::HairSS},
            {STR("Plastic SS Col"), PaletteSlot::HairSS},
            {STR("Crystal BaseCol"), PaletteSlot::Custom},
            {STR("GemColorMul"), PaletteSlot::Custom, true},
            {STR("GemDarkColor"), PaletteSlot::HairSS},
            // --- the same zones under each bespoke master's own
            // spelling. Keyblades with a custom shader keep the idea and
            // rename the parameter.
            {STR("MetalColorMulti"), PaletteSlot::Metal, true},
            {STR("SubMetalColorMulti"), PaletteSlot::Metal, true},
            {STR("Key_Metal_Base_Col_Multi"), PaletteSlot::Metal, true},
            {STR("metal_col"), PaletteSlot::Metal},
            // Wheel of Fate (w_so090) names its zones after what they
            // are rather than after the material family.
            {STR("Metal"), PaletteSlot::Metal, true},
            {STR("Wood"), PaletteSlot::Leather, true},
            {STR("Wood Mul"), PaletteSlot::Leather, true},
            {STR("Rope and Drapery"), PaletteSlot::Nuno, true},
            {STR("Skull"), PaletteSlot::Custom, true},
            // --- multipliers covering a whole weapon rather than one
            // zone. Forced to full value for the same reason the
            // Caribbean cast's are: these sit over dark metal textures
            // with no headroom to give away.
            {STR("BaseColMult"), PaletteSlot::BaseForced, true},
            {STR("BaseCol_Mult"), PaletteSlot::BaseForced, true},
            {STR("base_col_mult"), PaletteSlot::BaseForced, true},
            {STR("color_mult"), PaletteSlot::BaseForced, true},
            {STR("01_Base Col Mult"), PaletteSlot::BaseForced, true},
            {STR("SnowBaseColMult"), PaletteSlot::BaseForced, true},
            {STR("bottle_color_mult"), PaletteSlot::BaseForced, true},
            {STR("CoreCol_Mult"), PaletteSlot::Custom, true},
            // Base colours rather than multipliers - these replace the
            // surface outright, so they are left at drawn value.
            {STR("BaseCol"), PaletteSlot::Base},
            {STR("base_col"), PaletteSlot::Base},
            // --- subsurface, across every naming family at once.
            {STR("SSColorMulti"), PaletteSlot::HairSS},
            {STR("SSCol_Mult"), PaletteSlot::HairSS},
            {STR("SSColMult"), PaletteSlot::HairSS},
            {STR("SSCol_Dark_Mult"), PaletteSlot::HairSS},
            {STR("SnowSSColMult"), PaletteSlot::HairSS},
            {STR("Wing_SSColMulti"), PaletteSlot::HairSS},
            {STR("ss_col_mult"), PaletteSlot::HairSS},
            {STR("ss_add_col"), PaletteSlot::HairSS},
            {STR("Base_SSCol"), PaletteSlot::HairSS},
            {STR("05_SS Col"), PaletteSlot::HairSS},
            {STR("Mask01_SSSubCol"), PaletteSlot::HairSS},
            {STR("Mask02_SSSubCol"), PaletteSlot::HairSS},
            // --- Hero's Origin (w_so030), Zeus' shield.
            {STR("ZeusColorMulti"), PaletteSlot::Metal, true},
            {STR("Zeus_Col"), PaletteSlot::Custom},
            {STR("01_Base Light Col"), PaletteSlot::Nuno},
            {STR("01_Base No Cloud Col"), PaletteSlot::Leather},
            // --- the galaxy family: Ultima Weapon and Starlight
            // (w_so120/121/190/191) and Donald's Astrolabe (w_do090).
            // The stars take the hair slots so they land on a hue of
            // their own rather than merging into the metalwork.
            {STR("Galaxy_Col"), PaletteSlot::Nuno},
            {STR("Main_Galaxy_Col_0"), PaletteSlot::Nuno},
            {STR("Sub_Galaxy_Col_0"), PaletteSlot::Leather},
            {STR("Outer_Galaxy_Col"), PaletteSlot::Custom},
            {STR("Main_Star_Col"), PaletteSlot::Hair},
            {STR("Sub_Star_Col"), PaletteSlot::HairTips},
            {STR("Main_Star_Col_0"), PaletteSlot::Hair},
            {STR("Sub_Star_Col_0"), PaletteSlot::HairTips},
            {STR("Grad_00"), PaletteSlot::Base, true},
            {STR("Grad_01"), PaletteSlot::Metal, true},
            {STR("start_col"), PaletteSlot::Custom},
            // --- Ever After (w_so070/072). Rapunzel's hair shader
            // again, on a keyblade: the same four names the character
            // table already carries for her, plus the flowers.
            {STR("BaseHairColor"), PaletteSlot::Hair},
            {STR("DeepHairColor"), PaletteSlot::HairSS},
            {STR("SSHairColor"), PaletteSlot::HairSS},
            {STR("SS_D_HairColor"), PaletteSlot::HairSS},
            {STR("AddPlantCol"), PaletteSlot::Nuno},
            {STR("AddFlowerCol"), PaletteSlot::Custom},
            {STR("CenterEmissiveColMlti"), PaletteSlot::Custom, true},
            // --- Crystal Snow (w_so080..086).
            {STR("Color1"), PaletteSlot::Nuno},
            {STR("Color2"), PaletteSlot::Custom},
            {STR("Crack1Col"), PaletteSlot::Leather},
            {STR("lineCavityCol"), PaletteSlot::HairSS},
            {STR("Ca DarkCol"), PaletteSlot::Leather},
            {STR("Ca MiddleCol"), PaletteSlot::Metal},
            // --- Grand Chef (w_so140/141). The wine, not the glass:
            // the bottle's own Glass_Param/X_Axis/bottle_center set is
            // geometry and is excluded above.
            {STR("yellow_wine_color_mult"), PaletteSlot::Custom, true},
            {STR("green_wine_color_mult"), PaletteSlot::Nuno, true},
            // --- w_so073's masonry set: six zones, each with a base, a
            // raised and a recessed colour.
            {STR("RM_BaseCol"), PaletteSlot::Nuno},
            {STR("RM_ExtraCol"), PaletteSlot::Custom},
            {STR("RM_CaveCol"), PaletteSlot::HairSS},
            {STR("GM_BaseCol"), PaletteSlot::Leather},
            {STR("GM_ExtraCol"), PaletteSlot::Custom},
            {STR("GM_CaveCol"), PaletteSlot::HairSS},
            {STR("BM_01BaseCol"), PaletteSlot::Metal},
            {STR("BM_01ExtraCol"), PaletteSlot::Custom},
            {STR("BM_01CaveCol"), PaletteSlot::HairSS},
            {STR("BM_02BaseCol"), PaletteSlot::Metal},
            {STR("BM_02ExtraCol"), PaletteSlot::Custom},
            {STR("BM_02CaveCol"), PaletteSlot::HairSS},
            {STR("KM_BaseCol"), PaletteSlot::Base},
            {STR("KM_ExtraCol"), PaletteSlot::Custom},
            {STR("KM_CaveCol"), PaletteSlot::HairSS},
            {STR("AM_BaseCol"), PaletteSlot::Nuno},
            {STR("AM_ExtraCol"), PaletteSlot::Custom},
            {STR("AM_CaveCol"), PaletteSlot::HairSS},
            // --- Classic Tone (w_so060). Its one colour parameter
            // ships as a near-pure red LED, so this is the parameter
            // that reads as "the keyblade's colour" on that model.
            {STR("EmissiveColor"), PaletteSlot::Hair},
            // --- Favorite Deputy's fire (w_so142/143) and the
            // remaining one-off accents across Sora, Donald and Goofy.
            {STR("add_col"), PaletteSlot::Custom},
            {STR("add_front_col"), PaletteSlot::Metal},
            {STR("emissive_col"), PaletteSlot::Custom},
            {STR("ShadowSkinColorMul"), PaletteSlot::BaseForced, true},
            {STR("dirt col"), PaletteSlot::Leather},
            {STR("CubeCol"), PaletteSlot::Metal},
            {STR("CenterCol"), PaletteSlot::Custom},
            {STR("Col0"), PaletteSlot::Nuno},
            {STR("Col1"), PaletteSlot::Leather},
            {STR("AddBaseCol"), PaletteSlot::Base},
            {STR("ShadowCol"), PaletteSlot::HairSS},
        }};

        // "<Zone> Setting" is (metallic, specular, roughness), decoded from
        // the spread across all 150 shipped weapons: channel 1 is 0.500 in
        // nearly every instance, channel 2 varies 0.1-0.85, and channel 0
        // is 0.90-1.00 on every Metal zone but 0.000 on most Cloth, Leather
        // and Wood zones.
        //
        // That is the second reason a keyblade resists paint, independent
        // of the multiplier cap: a near-fully-metallic surface in UE4 has
        // almost no diffuse albedo - it shows the environment rather than
        // its own base colour - so the colour multiplier has very little to
        // act on. Kingdom Key's metal ships at 0.92. Raising the gain tints
        // the reflection harder but cannot beat that.
        //
        // This is the one deliberate exception to the "never write a
        // <Zone> Setting" rule stated on weapon_parameter_bindings, and it
        // is safe only because of how narrowly it is scoped: the current
        // value is READ FIRST, only channel 0 is changed, specular and
        // roughness are written back exactly as found, and the value is
        // only ever lowered. A zone that is already non-metallic is below
        // the threshold and is skipped, as is a material that does not
        // expose the parameter at all.
        constexpr std::array<std::wstring_view, 5> weapon_zone_setting_names{
            STR("Metal Setting"), STR("Plastic Setting"), STR("Leather Setting"),
            STR("Cloth Setting"), STR("Wood Setting"),
        };
        // Only zones the shipped data calls metal. Cloth and leather sit at
        // 0.0 and must not be touched.
        constexpr float weapon_metallic_threshold = 0.85f;
        // Enough sheen left to still read as metal, enough albedo for the
        // colour to be the thing you see.
        constexpr float weapon_metallic_target = 0.35f;

        auto relax_weapon_metal_zones(UObject* component, std::wstring_view weapon_key) -> void
        {
            const std::optional<int> count = read_num_materials(component);
            const int limit = std::min(count.value_or(0), max_weapon_material_slots);
            for (int index = 0; index < limit; ++index)
            {
                UObject* const material_instance =
                    create_dynamic_material_instance(component, index);
                if (!material_instance)
                {
                    continue;
                }
                for (const std::wstring_view setting : weapon_zone_setting_names)
                {
                    const std::optional<std::array<float, 4>> current =
                        read_vector_parameter_from_mid(material_instance, setting);
                    if (!current || (*current)[0] < weapon_metallic_threshold)
                    {
                        continue;
                    }
                    set_vector_parameter_on_mid(
                        material_instance, setting,
                        // Specular and roughness carried through
                        // untouched, so each weapon keeps its own
                        // finish rather than being flattened onto a
                        // shared constant.
                        DerivedColor{weapon_metallic_target, (*current)[1], (*current)[2]},
                        weapon_key, (*current)[3]);
                    if (s_debug_logging)
                    {
                        Output::send<LogLevel::Normal>(
                            STR("[KH3APProbeCpp] Color weapon metallic lowered key={} slot={} "
                                "param={} ") STR("metallic {}->{} specular={} roughness={}\n"),
                            weapon_key, index, setting, (*current)[0], weapon_metallic_target,
                            (*current)[1], (*current)[2]);
                    }
                }
            }
        }

        // One line per material slot on a weapon, naming the slot's
        // material, the asset it was instanced from, and the vector
        // parameters that asset overrides.
        //
        // This is the discovery half of the feature and it earns its place:
        // weapon_parameter_bindings was built by reading the shipped
        // material assets, which covers Sora's keyblades, Donald's and
        // Goofy's weapons, and nothing else. A DLC weapon, or one whose
        // asset was not in the extraction, will simply not move - and
        // without this line there is no way to tell that apart from the
        // feature being broken. Grep "Color weapon material slot".
        auto log_weapon_material_slots(UObject* component, std::wstring_view weapon_key) -> void
        {
            const std::optional<int> count = read_num_materials(component);
            const int limit = std::min(count.value_or(0), max_weapon_material_slots);
            for (int index = 0; index < limit; ++index)
            {
                UObject* const material = read_material_at(component, index);
                UObject* const parent = read_strong_object_property(material, STR("Parent"));
                // The asset, not the runtime instance: the same rule the
                // head detection follows, and for the same reason - the
                // game writes its own entries into the instances.
                const std::vector<std::wstring> names =
                    material_parameter_names(parent ? parent : material);
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Color weapon material slot key={} slot={} material={} ")
                        STR("parent={} vector_params={}\n"),
                    weapon_key, index, format_object_debug_name(material),
                    format_object_debug_name(parent), join_wide_parts(names, STR(",")));
            }
        }

        // How far past 1.0 each kind of weapon parameter is driven. All
        // three sit inside the range the shipped assets already use, so
        // none of this asks the shader for anything it does not do itself.
        //
        // The multiplier gain is the one that matters: a keyblade's colour
        // parameters multiply a texture that is mostly mid-grey metal, and
        // at a peak of 1.0 the best a draw can do is leave one channel
        // alone and darken the other two. At 2.0 it brightens as well as
        // shifts, which is the difference between a tinted keyblade and a
        // coloured one.
        constexpr float weapon_multiplier_gain = 2.0f;
        // Subsurface is a glow through the surface rather than the surface
        // itself; it ships as high as 3.0, but pushing it that far reads as
        // wax rather than as colour.
        constexpr float weapon_subsurface_gain = 1.5f;

        // Subsurface parameters across every weapon naming family. The
        // capital pair is the usual spelling ("Leather SS Col",
        // "SSColorMulti", "Mask01_SSSubCol"); two weapons use a lower-case
        // prefix instead, and no non-subsurface name in the table contains
        // either.
        [[nodiscard]] auto weapon_parameter_is_subsurface(std::wstring_view name) -> bool
        {
            return name.find(STR("SS")) != std::wstring_view::npos ||
                   name.find(STR("ss_")) != std::wstring_view::npos;
        }

        auto apply_weapon_colors(UObject* component, std::wstring_view weapon_key,
                                 std::wstring_view seed, RuntimeColorHarmonyMode mode) -> void
        {
            const CharacterPalette palette = build_palette(seed, weapon_key, mode);
            for (const ParameterBinding& binding : weapon_parameter_bindings)
            {
                const DerivedColor color = palette_color(palette, binding.slot);
                // full_value marks the multipliers; everything else in the
                // table is a base colour that replaces the surface outright
                // and is already as strong as it needs to be, so driving it
                // past 1.0 would only blow it out.
                const float gain =
                    binding.full_value
                        ? weapon_multiplier_gain
                        : (weapon_parameter_is_subsurface(binding.name) ? weapon_subsurface_gain
                                                                        : 1.0f);
                set_vector_parameter_on_materials(component, binding.name,
                                                  gain == 1.0f ? color : at_gain(color, gain),
                                                  weapon_key);
            }
            // After the colour pass, so the slots are already dynamic
            // instances and this only has to read and adjust them.
            relax_weapon_metal_zones(component, weapon_key);

            // No slot plan and no head softening: a weapon has no face to
            // protect, and its slots are already separated by the zone
            // masks the by-name pass writes through.

            if (s_debug_logging)
            {
                const auto hex = [](const DerivedColor& color)
                {
                    const auto channel = [](float value)
                    {
                        return static_cast<int>(
                            std::lround(std::clamp(value, 0.0f, 1.0f) * weapon_color_channel_max));
                    };
                    return std::format(STR("#{:02X}{:02X}{:02X}"), channel(color.r),
                                       channel(color.g), channel(color.b));
                };
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Color weapon applied key={} seed={} mode={} gain={} "
                        "metal={} ") STR("leather={} cloth={} accent={} base={}\n"),
                    weapon_key, seed.empty() ? std::wstring_view{STR("<none>")} : seed,
                    mode == RuntimeColorHarmonyMode::Wild ? std::wstring_view{STR("wild")}
                                                          : std::wstring_view{STR("coordinated")},
                    // The hues below are the drawn palette; multipliers go
                    // out scaled to this peak, so the log says both what
                    // was drawn and how hard it was pushed.
                    weapon_multiplier_gain, hex(palette.metal), hex(palette.leather),
                    hex(palette.nuno), hex(palette.custom), hex(palette.base_forced));
            }
        }
    } // namespace

    auto runtime_color_apply_equipped_weapons(RuntimeColorApplyState& state, std::wstring_view seed,
                                              RuntimeColorHarmonyMode mode) -> bool
    {
        UClass* const mesh_component_class = resolve_mesh_component_class();
        if (!mesh_component_class)
        {
            return false;
        }
        bool complete = true;
        // state.colored() is only read here; marking a weapon appends to
        // a different vector, so this iteration is not invalidated.
        for (UObject* const pawn : state.colored())
        {
            if (!pawn || !is_uobject_valid_fast(pawn))
            {
                continue;
            }
            for (UObject* const weapon : resolve_equipped_weapons(pawn))
            {
                if (state.is_weapon_colored(weapon))
                {
                    continue;
                }
                const std::wstring weapon_key = resolve_weapon_key(weapon);
                if (weapon_key.empty())
                {
                    continue;
                }
                std::vector<UObject*> targets =
                    collect_mesh_components(weapon, mesh_component_class);
                // The same floor the character path keeps: the
                // single-result lookup is the one confirmed to work on
                // these actors, so it is folded in rather than trusted
                // to be a subset of the enumeration.
                if (UObject* const single = first_component_by_class(weapon, mesh_component_class);
                    single && std::find(targets.begin(), targets.end(), single) == targets.end())
                {
                    targets.push_back(single);
                }
                bool painted = false;
                for (UObject* const target : targets)
                {
                    // SetVectorParameterValueOnMaterials creates the
                    // dynamic instance for any slot that is not one
                    // already, so it assumes a registered, render-state-
                    // initialized primitive exactly as
                    // CreateDynamicMaterialInstance does. A weapon
                    // actor spawned this frame may not be one yet; the
                    // gate fails open, so an unreadable property still
                    // paints.
                    if (!inspect_mesh_candidate(target, false).usable())
                    {
                        continue;
                    }
                    if (s_debug_logging)
                    {
                        log_weapon_material_slots(target, weapon_key);
                    }
                    apply_weapon_colors(target, weapon_key, seed, mode);
                    painted = true;
                }
                // Nothing usable yet is not a failure - leave the weapon
                // unmarked and pick it up on the next sweep.
                if (painted)
                {
                    state.mark_weapon_colored(weapon);
                    complete = complete && state.is_weapon_colored(weapon);
                }
                else
                {
                    complete = false;
                }
            }
        }
        return complete;
    }

    auto runtime_color_restore_weapons(const RuntimeColorApplyState& state) -> std::pair<int, int>
    {
        UClass* const mesh_component_class = resolve_mesh_component_class();
        if (!mesh_component_class)
        {
            return {0, 0};
        }
        int restored_weapons = 0;
        int restored_slots = 0;
        for (UObject* const weapon : state.colored_weapons())
        {
            if (!weapon || !is_uobject_valid_fast(weapon))
            {
                continue;
            }
            bool touched = false;
            for (UObject* const component : collect_mesh_components(weapon, mesh_component_class))
            {
                const std::optional<int> count = read_num_materials(component);
                const int limit = std::min(count.value_or(0), max_weapon_material_slots);
                for (int index = 0; index < limit; ++index)
                {
                    if (restore_shipped_material(component, index))
                    {
                        ++restored_slots;
                        touched = true;
                    }
                }
            }
            if (touched)
            {
                ++restored_weapons;
            }
        }
        return {restored_weapons, restored_slots};
    }
} // namespace kh3ap
