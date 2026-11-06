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
        constexpr auto max_character_slot_candidates = 24;
        constexpr auto slot_color_channel_max = 255.0f;
        using ::kh3ap::settings::s_debug_logging;
    } // namespace

    namespace
    {
        // Per-slot colouring, for the characters whose slots have been
        // identified body part by body part.
        //
        // The by-name pass (SetVectorParameterValueOnMaterials) writes every
        // slot at once, which is why "colour the clothes but not the face"
        // looked impossible on a toy: their zone masks overlap, so no subset
        // of parameter NAMES separates a face from a shirt. Slots do. Every
        // slot on these models is its own MaterialInstanceDynamic (confirmed
        // from the material manifest: Woody 24-29, Buzz 30-33, Toy Sora
        // 0-5), so a value written to one slot reaches only that slot.
        enum class SlotPolicy : uint8_t
        {
            Skip,  // left exactly as the game shipped it
            Paint, // full palette, zone variety preserved
            Soft,  // one soft tint across the whole slot
        };

        struct CharacterSlotBinding
        {
            std::wstring_view character_key;
            int slot;
            SlotPolicy policy;
            std::wstring_view part;
            // Widens this slot's GlossPlasticChannel so the gloss colour
            // reaches the whole slot instead of only the pixels the mask
            // texture already claims for it.
            //
            // Needed because every colour parameter the Toy PC shader
            // exposes is a multiplier, and a multiplier cannot brighten:
            // on Toy Sora's dark hair the palette can only tint, which is
            // why the isolating probe could turn it convincingly black
            // while a bright colour barely showed. The mask-widen probe
            // did produce a genuinely bright red head of hair on the
            // GlossPlastic step, so that is the one route to a real
            // recolour rather than a tint. Scoped to a single slot,
            // because widening it everywhere would gloss surfaces that
            // are meant to be matte.
            bool widen_gloss{false};
            // Empty means every world. Donald and Goofy keep the same
            // pawn keys in Toy Box as everywhere else, so their plan has
            // to be gated on the level or it would replace the feather
            // handling tuned for the rest of the game.
            std::wstring_view world{};
            // Marks the iris material, which takes its own parameter set.
            bool eyes{false};
        };

        // Slot identities come from the F10 per-slot probe, read off a
        // screenshot. The later isolating probe disagreed about Woody, and
        // this table follows the per-slot reading by explicit user decision
        // - if a body part comes out wrong in game, this table is the one
        // place to correct it, and the disagreement is documented in
        // COLOR_RANDOMIZATION_NOTES.md rather than resolved.
        //
        // Slots marked unknown showed no visible change under any probe.
        // They are painted rather than skipped precisely because of that:
        // a slot nothing can be seen on costs nothing to colour.
        constexpr std::array<CharacterSlotBinding, 28> character_slot_bindings{{
            // Woody. Face and hands share slot 3, so that one slot is the
            // whole of "do not paint his face" - the thing the by-name
            // pass could never express.
            {STR("n_ts201"), 0, SlotPolicy::Paint, STR("hat")},
            {STR("n_ts201"), 1, SlotPolicy::Paint, STR("trousers")},
            {STR("n_ts201"), 2, SlotPolicy::Paint, STR("unknown")},
            {STR("n_ts201"), 3, SlotPolicy::Skip, STR("face+hands")},
            {STR("n_ts201"), 4, SlotPolicy::Paint, STR("shirt+vest")},
            {STR("n_ts201"), 5, SlotPolicy::Paint, STR("unknown/hair")},
            // Buzz. His head is part of the body-suit material, so slot 0
            // cannot be repainted without repainting his face - the same
            // bind Donald's white feathers are in, and it takes the same
            // answer: tint in Coordinated, repaint in Wild.
            // His suit, arms, legs AND head are all one slot, and there
            // is no atama marker anywhere on Buzz to tell the face apart.
            // Painting it colours his face; a soft tint was worse still,
            // nearly invisible on the white suit yet enough to leave the
            // face grey. So Coordinated leaves the whole slot alone and
            // lets the visor and armour carry him - his suit stays the
            // white everyone recognises. Wild repaints it, face included,
            // which is what that mode is for. Chosen deliberately over
            // hunting for the head zone.
            {STR("n_ts202"), 0, SlotPolicy::Skip, STR("suit+arms+legs+head")},
            {STR("n_ts202"), 1, SlotPolicy::Paint, STR("visor glass")},
            {STR("n_ts202"), 2, SlotPolicy::Paint, STR("unknown")},
            {STR("n_ts202"), 3, SlotPolicy::Paint, STR("armour")},
            // Toy Sora. Slot 3 is the only material in the whole dump
            // that the atama test flags as a head. Slots 1 and 2 run
            // p_ts001_Basemat, a second master that nothing wrote to
            // before - its SkinColorMulti lived in the creature table,
            // and he is not a creature.
            {STR("p_ts001"), 0, SlotPolicy::Paint, STR("hair"), true},
            // His fingers, neck and legs are the only large surfaces on
            // him that a colour parameter can actually reach - the
            // jacket, shorts and boots render from BaseColorTex, which
            // no mask claims and no parameter can touch. A soft tint
            // there wasted the only surfaces available, so they are
            // painted properly. He is a plastic toy; recoloured limbs
            // read as a different figurine rather than as a rash, which
            // is the reason skin is kept off human-shaped characters.
            {STR("p_ts001"), 1, SlotPolicy::Paint, STR("fingers+neck skin")},
            {STR("p_ts001"), 2, SlotPolicy::Paint, STR("unknown")},
            {STR("p_ts001"), 3, SlotPolicy::Skip, STR("face")},
            {STR("p_ts001"), 4, SlotPolicy::Paint, STR("legs")},
            {STR("p_ts001"), 5, SlotPolicy::Paint, STR("unknown")},
            // Donald and Goofy in Toy Box. Gated to ts_01: the same keys
            // carry their normal models everywhere else, where the tuned
            // feather behaviour (soft in Coordinated, so he stays a white
            // duck) still applies.
            //
            // Eye slots are Skip, which is exactly "eyes only in Wild" -
            // Wild overrides every policy to Paint.
            {STR("n_ex001"), 0, SlotPolicy::Paint, STR("feathers+head"), false, STR("ts_01"),
             false},
            {STR("n_ex001"), 1, SlotPolicy::Paint, STR("clothes"), false, STR("ts_01"), false},
            {STR("n_ex001"), 2, SlotPolicy::Skip, STR("eyes"), false, STR("ts_01"), true},
            {STR("n_ex002"), 0, SlotPolicy::Paint, STR("clothes"), false, STR("ts_01"), false},
            {STR("n_ex002"), 1, SlotPolicy::Paint, STR("clothes"), false, STR("ts_01"), false},
            // His muzzle and hands. Softened, not painted: this slot is
            // his face, and the toys set the precedent that a face stays
            // recognisable. Flip to Paint if a fully recoloured Goofy is
            // wanted.
            {STR("n_ex002"), 2, SlotPolicy::Soft, STR("skin/muzzle"), false, STR("ts_01"), false},
            {STR("n_ex002"), 3, SlotPolicy::Skip, STR("eyes"), false, STR("ts_01"), true},
            // Toy Box Donald and Goofy. These are the real toy models -
            // n_ts001/n_ts002, NOT the n_ex001/n_ex002 above - on a
            // seventh master, SQEX_ToyNPC_Basemat (Toy Sora runs
            // SQEX_ToyPC_Basemat; PC vs NPC).
            //
            // Every colour parameter they expose was already in the
            // universal table, so nothing was missing - they were simply
            // never reached: both keys sit in toy_character_keys, which
            // returns before any painting outside Wild. A slot plan is
            // what gets past that.
            //
            // No world gate: these keys only ever carry the toy models,
            // including where they stand in GoA, and colouring them there
            // is correct too.
            {STR("n_ts001"), 0, SlotPolicy::Paint, STR("body+head")},
            {STR("n_ts001"), 1, SlotPolicy::Skip, STR("eyes"), false, {}, true},
            {STR("n_ts002"), 0, SlotPolicy::Paint, STR("body+head")},
            {STR("n_ts002"), 1, SlotPolicy::Skip, STR("eyes"), false, {}, true},
            {STR("n_ts002"), 2, SlotPolicy::Paint, STR("body")},
        }};

        [[nodiscard]] auto binding_applies(const CharacterSlotBinding& binding,
                                           std::wstring_view character_key,
                                           std::wstring_view world_name) -> bool
        {
            return binding.character_key == character_key &&
                   (binding.world.empty() ||
                    world_name.find(binding.world) != std::wstring_view::npos);
        }

    } // namespace

    [[nodiscard]] auto character_has_slot_plan(std::wstring_view character_key,
                                               std::wstring_view world_name) -> bool
    {
        return std::any_of(character_slot_bindings.begin(), character_slot_bindings.end(),
                           [&](const CharacterSlotBinding& binding)
                           {
                               return binding_applies(binding, character_key, world_name);
                           });
    }

    auto apply_slot_plan_colors(UObject* component, std::wstring_view character_key,
                                std::wstring_view world_name, std::wstring_view seed,
                                RuntimeColorHarmonyMode mode) -> void
    {
        const bool wild = (mode == RuntimeColorHarmonyMode::Wild);
        const std::optional<int> count = read_num_materials(component);
        const int limit = std::min(count.value_or(0), max_character_slot_candidates);
        // Counts only the slots actually painted, so a skipped face
        // does not consume a hue and leave a gap in the spread.
        int painted_rank = 0;
        for (const CharacterSlotBinding& binding : character_slot_bindings)
        {
            if (!binding_applies(binding, character_key, world_name) || binding.slot >= limit)
            {
                continue;
            }
            // Wild repaints everything, faces included - same rule the
            // feather and toy handling already follow.
            const SlotPolicy policy = wild ? SlotPolicy::Paint : binding.policy;
            if (policy == SlotPolicy::Skip)
            {
                // Restored, not merely left alone. Skipping only means
                // "write nothing", which does not undo what a previous
                // apply put on the material - so coming back from Wild,
                // where every slot is painted, left Woody and Buzz with
                // repainted faces that Coordinated then declined to
                // touch. Putting the shipped material back makes a
                // skipped slot genuinely vanilla whatever preceded it.
                (void)restore_shipped_material(component, binding.slot);
                continue;
            }
            // One base hue for the character, rotated per slot, rather
            // than an independent hue per slot. Independent draws do
            // vary - but nothing stops them colliding, and a live draw
            // where the hat, shirt and trousers all landed near 310
            // degrees produced a uniformly pink Woody. The golden angle
            // keeps consecutive slots far apart for any number of them
            // and never repeats, so separation is a property of the
            // construction instead of a property of luck.
            //
            // Zones INSIDE a slot stay analogous (nuno/leather/metal/
            // custom sit within about 36 degrees of each other), so a
            // garment is still internally coherent - it is the garments
            // that now differ from one another.
            constexpr float golden_angle = 137.507764f;
            const CharacterPalette palette = build_palette(
                seed, character_key, mode, static_cast<float>(painted_rank) * golden_angle);
            UObject* const material_instance =
                create_dynamic_material_instance(component, binding.slot);
            if (!material_instance)
            {
                continue;
            }
            const auto write = [&](const ParameterBinding& parameter)
            {
                // Soft flattens the whole slot to one gentle tint, which
                // is what a face or a dominant single-colour zone wants.
                // Paint keeps each zone on its own palette entry, so a
                // plaid shirt stays plaid.
                const PaletteSlot target =
                    (policy == SlotPolicy::Soft) ? PaletteSlot::BaseSoft : parameter.slot;
                // Soft resolves to BaseSoft, whose gain is 1.0, so a
                // face or a dominant single-colour zone is left exactly
                // as gentle as it was - the separation this path exists
                // for survives the lift untouched.
                set_vector_parameter_on_mid(
                    material_instance, parameter.name,
                    character_color_for_slot(palette, target, parameter.full_value), character_key);
            };
            for (const ParameterBinding& parameter : universal_parameter_bindings)
            {
                write(parameter);
            }
            // The skin family as well, unconditionally - the creature
            // gate does not apply here. Toy Sora is not a creature, so
            // SkinColorMulti was never written for him, and it is what
            // actually drives his fingers, neck and legs: the log showed
            // slot 1 taking #9CFB32 and slot 4 #FB3E32 with nothing
            // visible changing, because every name being written missed
            // the one that owns those surfaces. The old per-slot probe
            // did colour his legs, and only because it wrote every known
            // name rather than one table.
            //
            // Safe to write everywhere in this path: a slot plan only
            // exists for the Toy Box cast, and a parameter a material
            // does not expose is a silent no-op.
            for (const ParameterBinding& parameter : creature_parameter_bindings)
            {
                write(parameter);
            }
            for (const ParameterBinding& parameter : disney_parameter_bindings)
            {
                write(parameter);
            }
            if (binding.eyes)
            {
                for (const ParameterBinding& parameter : eye_parameter_bindings)
                {
                    write(parameter);
                }
            }
            if (binding.widen_gloss)
            {
                constexpr DerivedColor claim_all{1.0f, 1.0f, 1.0f};
                constexpr DerivedColor claim_none{0.0f, 0.0f, 0.0f};
                // Widening the gloss selector on its own was not
                // enough - live result was hair still only tinted.
                // The mask probe that DID produce bright red hair
                // also switched the competing families off, and that
                // is the part that matters: with the plastic family
                // still claiming the same pixels, its plain multiply
                // over dark hair wins and the gloss colour is lost
                // underneath it. Silencing them leaves gloss as the
                // only family on this slot.
                for (const std::wstring_view mask :
                     {STR("PlasticMaskChannel"), STR("MetalMaskChannell"),
                      STR("ClearPrasticMaskChannel")})
                {
                    set_vector_parameter_on_mid(material_instance, mask, claim_none, character_key);
                }
                set_vector_parameter_on_mid(material_instance, STR("GlossPlasticChannel"),
                                            claim_all, character_key);
                // Driven with the hair colour rather than whatever
                // the table maps GlossPlasticColorMulti to (Metal,
                // which sits at half saturation and tints weakly).
                // This is now the only thing colouring the slot, so
                // it should carry the full hair hue.
                // Toy Sora's hair is the case the 1.0 cap hurt most -
                // near-black, so a multiplier at peak 1.0 could turn it
                // convincingly darker but never actually bright. Now
                // that gloss is the only family left on the slot, it
                // takes the hair hue at the hair slot's gain.
                set_vector_parameter_on_mid(
                    material_instance, STR("GlossPlasticColorMulti"),
                    at_gain(palette.hair, character_slot_gain(PaletteSlot::Hair)), character_key);
            }
            if (s_debug_logging)
            {
                const auto hex = [](const DerivedColor& color)
                {
                    const auto channel = [](float value)
                    {
                        return static_cast<int>(
                            std::lround(std::clamp(value, 0.0f, 1.0f) * slot_color_channel_max));
                    };
                    return std::format(STR("#{:02X}{:02X}{:02X}"), channel(color.r),
                                       channel(color.g), channel(color.b));
                };
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Color slot painted key={} slot={} part={} policy={} ")
                        STR("rank={} nuno={} base={} hair={}\n"),
                    character_key, binding.slot, binding.part,
                    policy == SlotPolicy::Soft ? std::wstring_view{STR("soft")}
                                               : std::wstring_view{STR("paint")},
                    painted_rank, hex(palette.nuno), hex(palette.base), hex(palette.hair));
            }
            ++painted_rank;
        }

        // Ungated, unlike the per-slot lines above, and deliberately
        // sharing the "Color applied" prefix.
        //
        // apply_character_colors returns as soon as it has called this,
        // so its own "Color applied" never fires for a slot-plan
        // character - the whole Toy Box cast. In a release log they went
        // from "pawn queued" straight to silence, which is
        // indistinguishable from having failed. That ambiguity is the
        // one that costs the most time when reading someone else's log.
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Color applied key={} seed={} mode={} path=slot_plan slots={}\n"),
            character_key, seed.empty() ? std::wstring_view{STR("<none>")} : seed,
            mode == RuntimeColorHarmonyMode::Wild ? std::wstring_view{STR("wild")}
                                                  : std::wstring_view{STR("coordinated")},
            painted_rank);
    }
} // namespace kh3ap::color_service_detail
