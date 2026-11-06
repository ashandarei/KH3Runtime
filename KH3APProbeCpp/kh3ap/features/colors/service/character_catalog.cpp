#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"

#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <span>

namespace kh3ap::color_service_detail
{
    using namespace RC::Unreal;

    // Every recruitable party-member pawn from RuntimePartyMemberService's
    // party_choices table (party_member_service.cpp), including
    // Donald/Goofy's own world variants (Caribbean/Monstropolis/Toy Box)
    // and every guest (Hercules, Woody, Buzz, Rapunzel, Flynn, Sulley,
    // Mike, Jack Sparrow, Baymax, Mickey, Riku, Aqua, Ventus, Lea, Kairi,
    // Roxas, Xion). Kept as a standalone list rather than a shared header
    // because that table carries UI labels and unlock plumbing this
    // service has no use for - keep the two lists in sync by hand if
    // party_choices gains or loses a pawn id.
    constexpr std::array<std::wstring_view, 25> companion_character_keys{
        STR("n_ex001"), STR("n_ex002"), STR("n_he201"), STR("n_ts201"), STR("n_ts202"),
        STR("n_ra201"), STR("n_ra203"), STR("n_mi201"), STR("n_mi202"), STR("n_ca201"),
        STR("n_bx202"), STR("n_ex003"), STR("n_ex004"), STR("n_ex011"), STR("n_ex006"),
        STR("n_ex010"), STR("n_ex005"), STR("n_ex009"), STR("n_ex033"), STR("n_ca001"),
        STR("n_ca002"), STR("n_mi001"), STR("n_mi002"), STR("n_ts001"), STR("n_ts002"),
    };

    // The Links (summons). Each one is its own pawn class, <key>_Pawn_C,
    // derived from ATresDisneyMagicPawnBase - the same naming the
    // companions use, so the companion scan and the key lookup below
    // take them as they are. Wandanyan is Meow Wow.
    constexpr std::array<std::wstring_view, 5> link_character_keys{
        STR("Stitch"), STR("Wandanyan"), STR("Ariel"), STR("Ralph"), STR("Simba"),
    };

    [[nodiscard]] auto character_is_link(std::wstring_view character_key) -> bool
    {
        return std::find(link_character_keys.begin(), link_character_keys.end(), character_key) !=
               link_character_keys.end();
    }

    // Every world-variant Sora pawn (Baymax, Toy Box, Monstropolis, ...)
    // and every companion/guest party-member pawn gets its own colours,
    // keyed by its own pawn id, so a seed does not hand two different
    // pawns the same palette.
    [[nodiscard]] auto resolve_supported_character_key(UObject* pawn) -> std::optional<std::wstring>
    {
        if (!pawn || !is_uobject_valid_fast(pawn))
        {
            return std::nullopt;
        }
        UClass* const klass = pawn->GetClassPrivate();
        if (!klass || !is_uobject_valid_fast(klass))
        {
            return std::nullopt;
        }
        const std::wstring class_name = klass->GetName();
        if (const RuntimeSoraPawnCatalogEntry* const entry =
                runtime_sora_pawn_catalog_match(class_name))
        {
            return std::wstring{entry->pawn_id};
        }
        for (const auto& keys : {std::span<const std::wstring_view>{companion_character_keys},
                                 std::span<const std::wstring_view>{link_character_keys}})
        {
            for (const std::wstring_view key : keys)
            {
                if (class_name == std::wstring{key} + STR("_Pawn_C"))
                {
                    return std::wstring{key};
                }
            }
        }
        return std::nullopt;
    }

    // Characters whose skin IS their costume: monsters, toys, a robot, a
    // bear, a rat. Recolouring skin on these is the whole point - Sulley
    // without his fur recoloured is barely touched. On a human-shaped
    // character the same parameters tint the face, arms and legs and
    // just read as a rash (confirmed live on Sora), so the skin family
    // is limited to this list rather than driven everywhere.
    // Toy Box characters are deliberately absent: a toy still has a
    // painted face, and recolouring it turned Woody's face green and
    // Sora's plastic skin orange while their clothes carried the colour
    // perfectly well on their own. They are coloured slot by slot now
    // (character_slot_bindings), which protects a face far better than
    // this list ever did; the exclusion is kept so the by-name fallback
    // stays safe for any toy added without a plan.
    constexpr std::array<std::wstring_view, 10> creature_character_keys{
        STR("p_mi001"),
        STR("n_mi001"),
        STR("n_mi002"),
        STR("n_mi201"),
        STR("n_mi202"),
        STR("p_bx001"),
        STR("n_bx202"),
        STR("p_re001"),
        // Stitch and Meow Wow: skin and fur are the whole character.
        STR("Stitch"),
        STR("Wandanyan"),
    };
    // p_po001 was here. Pooh-world Sora is a plush Sora wearing his own
    // outfit, so his clothes are the costume and his skin is skin - the
    // opposite of the monsters this list exists for. It cost nothing
    // until the Caribbean survey added the lower-case skinColorMulti,
    // which his pc_skinbasemat does expose, and he came out with a
    // painted face and body.

    // Toy Box characters. Their models split into many numbered zones
    // (Mat01..Mat09) whose masks overlap, so no subset of zones colours
    // a toy's clothes without also colouring its face - see the early
    // return in apply_character_colors.
    constexpr std::array<std::wstring_view, 5> toy_character_keys{
        STR("p_ts001"), STR("n_ts001"), STR("n_ts002"), STR("n_ts201"), STR("n_ts202"),
    };

    [[nodiscard]] auto character_is_toy(std::wstring_view character_key) -> bool
    {
        return std::find(toy_character_keys.begin(), toy_character_keys.end(), character_key) !=
               toy_character_keys.end();
    }

    [[nodiscard]] auto character_is_creature(std::wstring_view character_key) -> bool
    {
        return std::find(creature_character_keys.begin(), creature_character_keys.end(),
                         character_key) != creature_character_keys.end();
    }

    // The level name out of an object's full name, e.g. "ts_01". Same
    // parse world_is_allowed uses, kept separate because that one also
    // records the first sighting for diagnostics and is a member.
    [[nodiscard]] auto resolve_world_name(UObject* object) -> std::wstring
    {
        std::wstring full_name{};
        if (!object || !try_get_uobject_full_name_fast(object, full_name))
        {
            return std::wstring{};
        }
        const size_t colon = full_name.find(STR(":PersistentLevel"));
        if (colon == std::wstring::npos)
        {
            return std::wstring{};
        }
        const size_t dot = full_name.rfind(STR('.'), colon);
        if (dot == std::wstring::npos)
        {
            return std::wstring{};
        }
        return full_name.substr(dot + 1, colon - dot - 1);
    }
} // namespace kh3ap::color_service_detail
