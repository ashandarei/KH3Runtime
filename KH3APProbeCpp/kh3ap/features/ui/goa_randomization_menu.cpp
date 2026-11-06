#include "kh3ap/core/settings/seed_gameplay_options.h"
#include "kh3ap/features/ui/goa_options_menu.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/features/enemies/enemy_remap_service.h"
#include "kh3ap/features/party/party_member_service.h"

#include <UnrealDef.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto first_party_friend_slot = 1;
        constexpr auto second_party_friend_slot = 2;

        // Display order is independent of the persisted party choice IDs.
        constexpr std::array<std::size_t, 27> party_choice_order{{
            0,  26, 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
            13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
        }};

        [[nodiscard]] auto party_choice_index(std::size_t choice) -> std::size_t
        {
            for (std::size_t index = 0; index < party_choice_order.size(); ++index)
            {
                if (party_choice_order[index] == choice)
                {
                    return index;
                }
            }
            return 0;
        }
    } // namespace

    auto make_randomization_runtime_options_menu(RuntimeEnemyRemapService& enemy_remaps,
                                                 RuntimePartyMemberService& party_members,
                                                 RuntimeGoaSettingsService& goa_settings)
        -> RuntimeOptionsMenuDefinition
    {
        RuntimeOptionsMenuBuilder menu{
            STR("randomization"), STR("Randomization"),
            STR("Configure party members, enemy and boss randomization, and character colors.")};
        auto page = menu.page();
        page.toggle(
            STR("party"), STR("Randomize Party Members"),
            [&party_members]()
            {
                return party_members.status().randomization_enabled;
            },
            [&party_members](bool enabled)
            {
                (void)party_members.set_randomization_enabled(enabled);
            },
            [&party_members]()
            {
                return party_members.status().available;
            },
            [&party_members]()
            {
                const RuntimePartyMemberStatus status = party_members.status();
                return std::format(STR("Party member randomization is {}. Random pairs "
                                       "are selected from {} available party members. "
                                       "Donald and Goofy are always available; other "
                                       "members require their AP items."),
                                   status.randomization_enabled ? STR("ON") : STR("OFF"),
                                   status.unlocked_count);
            },
            []()
            {
                return std::wstring{STR("Choose ON for unlocked random party members, or "
                                        "OFF to use the Friend 1 and Friend 2 selections.")};
            });

        page.toggle(
            STR("party_chaos"), STR("Reroll Party Every Room"),
            [&party_members]()
            {
                return party_members.status().reroll_every_room;
            },
            [&party_members](bool enabled)
            {
                (void)party_members.set_reroll_every_room(enabled);
            },
            [&party_members]()
            {
                const RuntimePartyMemberStatus status = party_members.status();
                return status.available && status.randomization_enabled;
            },
            [&party_members]()
            {
                const RuntimePartyMemberStatus status = party_members.status();
                return std::format(STR("Party member rerolling on room change is {}. "
                                       "This row is available while Randomize Party "
                                       "Members is ON."),
                                   status.reroll_every_room ? STR("ON") : STR("OFF"));
            },
            []()
            {
                return std::wstring{STR("Choose ON to reroll the unlocked random pair on "
                                        "each room change, or OFF to keep the pair stable "
                                        "until available Party Member items change.")};
            });

        const auto party_choices = [&party_members](int32_t friend_slot)
        {
            std::vector<RuntimeOptionsChoice> choices{};
            choices.reserve(party_choice_order.size());
            for (const std::size_t choice : party_choice_order)
            {
                choices.push_back(RuntimeOptionsChoice{
                    .id = std::format(STR("party_{}"), choice),
                    .label = std::wstring{party_members.choice_label(choice)},
                    .available =
                        [&party_members, friend_slot, choice]()
                    {
                        return party_members.choice_available(friend_slot, choice);
                    },
                });
            }
            return choices;
        };

        page.choice(
            STR("party_friend_1"), STR("Friend 1"), party_choices(first_party_friend_slot),
            [&party_members]()
            {
                return party_choice_index(party_members.status().friend_1_choice);
            },
            [&party_members](std::size_t selected)
            {
                (void)party_members.set_friend_choice(first_party_friend_slot,
                                                      party_choice_order.at(selected));
            },
            [&party_members]()
            {
                const RuntimePartyMemberStatus status = party_members.status();
                return status.available && !status.randomization_enabled;
            },
            [&party_members]()
            {
                return std::format(STR("Select Friend 1 from available party members. "
                                       "{} party members are available. "
                                       "This row is available while Randomize Party "
                                       "Members is OFF."),
                                   party_members.status().unlocked_count);
            },
            []()
            {
                return std::wstring{STR("Vanilla restores the game's choice now. None leaves "
                                        "an empty slot. Explicit members win duplicates. Locked "
                                        "members and the character already selected for "
                                        "Friend 2 are unavailable.")};
            });

        page.choice(
            STR("party_friend_2"), STR("Friend 2"), party_choices(second_party_friend_slot),
            [&party_members]()
            {
                return party_choice_index(party_members.status().friend_2_choice);
            },
            [&party_members](std::size_t selected)
            {
                (void)party_members.set_friend_choice(second_party_friend_slot,
                                                      party_choice_order.at(selected));
            },
            [&party_members]()
            {
                const RuntimePartyMemberStatus status = party_members.status();
                return status.available && !status.randomization_enabled;
            },
            [&party_members]()
            {
                return std::format(STR("Select Friend 2 from available party members. "
                                       "{} party members are available. "
                                       "This row is available while Randomize Party "
                                       "Members is OFF."),
                                   party_members.status().unlocked_count);
            },
            []()
            {
                return std::wstring{STR("Vanilla restores the game's choice now. None leaves "
                                        "an empty slot. Explicit members win duplicates. Locked "
                                        "members and the character already selected for "
                                        "Friend 1 are unavailable.")};
            });

        page.toggle(
            STR("enemy"), STR("Enemy Randomization"),
            [&enemy_remaps]()
            {
                return enemy_remaps.status().enemy_enabled;
            },
            [&enemy_remaps](bool enabled)
            {
                (void)enemy_remaps.set_enemy_enabled(enabled);
            },
            [&enemy_remaps]()
            {
                return enemy_remaps.status().enemy_available;
            },
            [&enemy_remaps]()
            {
                const RuntimeEnemyRemapStatus status = enemy_remaps.status();
                return status.enemy_available
                           ? std::format(STR("Enemy randomization is {}. Select this row to "
                                             "choose ON or OFF; changes affect subsequent "
                                             "encounter spawns."),
                                         status.enemy_enabled ? STR("ON") : STR("OFF"))
                           : std::wstring{STR("Enemy randomization is unavailable for this seed.")};
            },
            []()
            {
                return std::wstring{STR("Choose ON or OFF. Changes affect subsequent "
                                        "encounter spawns.")};
            });

        page.toggle(
            STR("boss"), STR("Boss Randomization"),
            [&enemy_remaps]()
            {
                return enemy_remaps.status().boss_enabled;
            },
            [&enemy_remaps](bool enabled)
            {
                (void)enemy_remaps.set_boss_enabled(enabled);
            },
            [&enemy_remaps]()
            {
                return enemy_remaps.status().boss_available;
            },
            [&enemy_remaps]()
            {
                const RuntimeEnemyRemapStatus status = enemy_remaps.status();
                return status.boss_available
                           ? std::format(STR("Boss randomization is {}. Select this row to "
                                             "choose ON or OFF; changes affect subsequent "
                                             "encounter spawns."),
                                         status.boss_enabled ? STR("ON") : STR("OFF"))
                           : std::wstring{STR("Boss randomization is unavailable for this seed.")};
            },
            []()
            {
                return std::wstring{STR("Choose ON or OFF. Changes affect subsequent "
                                        "encounter spawns.")};
            });

        page.choice(
            STR("color"), STR("Palettesanity"),
            {
                {STR("0"), STR("OFF")},
                {STR("1"), STR("COORDINATED")},
                {STR("2"), STR("WILD")},
            },
            // The row index IS the mode: 0 off, 1 coordinated, 2 wild,
            // in the same order as the choices above.
            [&goa_settings]() -> std::size_t
            {
                const int32_t mode = goa_settings.status().color_randomization_mode;
                return static_cast<std::size_t>(mode < seed_color_mode_off ||
                                                        mode > seed_color_mode_wild
                                                    ? seed_color_mode_coordinated
                                                    : mode);
            },
            [&goa_settings](std::size_t index)
            {
                (void)goa_settings.set_color_randomization_mode(static_cast<int32_t>(index));
            },
            []()
            {
                return true;
            },
            [&goa_settings]()
            {
                const int32_t mode = goa_settings.status().color_randomization_mode;
                return std::format(
                    STR("Character colors are {}. Select this row to choose "
                        "OFF, COORDINATED or WILD; changes apply to the "
                        "current characters."),
                    mode == 0 ? STR("OFF")
                              : (mode == seed_color_mode_wild ? STR("WILD") : STR("COORDINATED")));
            },
            []()
            {
                return std::wstring{STR("COORDINATED gives each character one outfit hue and "
                                        "keeps faces recognisable. WILD repaints everything, "
                                        "faces included.")};
            });

        menu.on_host_ready(
            [&enemy_remaps]()
            {
                (void)enemy_remaps.request_loaded_toggle_class_prewarm();
            });
        return std::move(menu).build();
    }
} // namespace kh3ap
