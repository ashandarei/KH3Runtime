#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/ui/goa_options_menu.h"
#include "kh3ap/core/input/controller_preferences_service.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/features/enemies/experience_multiplier_math.h"
#include "kh3ap/features/enemies/experience_multiplier_service.h"

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
        constexpr std::array<int32_t, 10> battle_level_offsets{{
            -3,
            -2,
            -1,
            0,
            1,
            2,
            3,
            4,
            5,
            10,
        }};

        constexpr std::size_t maximum_deathlink_amnesty = 20;
        constexpr std::size_t zero_battle_level_offset_choice = 3;
        constexpr int32_t experience_half_steps_per_unit = 2;

        void add_deathlink_options(RuntimeOptionsPageBuilder& page,
                                   std::function<std::optional<std::filesystem::path>()> bridge_dir)
        {
            // Preferences are read only when the menu queries them. The client
            // initializes seed defaults and owns the network tag and death count.
            const auto read = [bridge_dir]() -> std::optional<std::array<std::size_t, 2>>
            {
                const auto dir = bridge_dir();
                if (!dir)
                {
                    return std::nullopt;
                }
                const auto text = read_text_file(*dir / STR("deathlink_settings.json"));
                if (!text || JsonFieldReader::int64_field(*text, "schema_version") != 1)
                {
                    return std::nullopt;
                }
                const auto enabled = JsonFieldReader::int64_field(*text, "enabled");
                const auto amnesty = JsonFieldReader::int64_field(*text, "amnesty");
                if (!enabled || *enabled < 0 || *enabled > 1 || !amnesty || *amnesty < 0 ||
                    *amnesty > maximum_deathlink_amnesty)
                {
                    return std::nullopt;
                }
                return std::array<std::size_t, 2>{static_cast<std::size_t>(*enabled),
                                                  static_cast<std::size_t>(*amnesty)};
            };
            const auto write = [bridge_dir, read](std::size_t field, std::size_t value)
            {
                if (field > 1 || value > (field == 0 ? 1u : maximum_deathlink_amnesty))
                {
                    return;
                }
                const auto dir = bridge_dir();
                auto values = read();
                if (!dir || !values)
                {
                    return;
                }
                (*values)[field] = value;
                (void)write_text_file_atomic_verified(
                    *dir / STR("deathlink_settings.json"),
                    std::format("{{\"schema_version\":1,\"enabled\":{},\"amnesty\":{}}}\n",
                                (*values)[0], (*values)[1]));
            };
            const auto available = [read]()
            {
                return read().has_value();
            };
            page.toggle(
                STR("deathlink"), STR("DeathLink"),
                [read]()
                {
                    const auto values = read();
                    return values && (*values)[0] != 0;
                },
                [write](bool enabled)
                {
                    write(0, enabled ? 1 : 0);
                },
                available,
                []()
                {
                    return std::wstring{STR("Share deaths with other DeathLink players.")};
                },
                []()
                {
                    return std::wstring{
                        STR("Turning OFF stops sending and receiving new DeathLinks.")};
                });
            std::vector<RuntimeOptionsChoice> choices{};
            for (std::size_t value = 0; value <= maximum_deathlink_amnesty; ++value)
            {
                choices.push_back(
                    {std::format(STR("amnesty_{}"), value),
                     value == 0 ? STR("0 (Every Death)") : std::format(STR("{}"), value)});
            }
            const auto help = []()
            {
                return std::wstring{STR("Forgive this many local deaths before sharing the next. 3 "
                                        "shares every fourth death. "
                                        "Changes reset the allowance.")};
            };
            page.choice(
                STR("deathlink_amnesty"), STR("DeathLink Amnesty"), std::move(choices),
                [read]()
                {
                    const auto values = read();
                    return values ? (*values)[1] : 0;
                },
                [write](std::size_t value)
                {
                    write(1, value);
                },
                available, help, help);
        }

        [[nodiscard]] auto battle_level_offset_choice_index(int32_t offset) -> std::size_t
        {
            for (std::size_t index = 0; index < battle_level_offsets.size(); ++index)
            {
                if (battle_level_offsets[index] == offset)
                {
                    return index;
                }
            }
            return zero_battle_level_offset_choice;
        }

        auto format_battle_level_offset(int32_t offset) -> std::wstring
        {
            return offset > 0 ? std::format(STR("+{}"), offset) : std::format(STR("{}"), offset);
        }

        auto format_experience_multiplier(int32_t half_steps) -> std::wstring
        {
            return half_steps % experience_half_steps_per_unit == 0
                       ? std::format(STR("{}x"), half_steps / experience_half_steps_per_unit)
                       : std::format(STR("{}.5x"), half_steps / experience_half_steps_per_unit);
        }
    } // namespace

    auto
    make_goa_runtime_options_menu(RuntimeExperienceMultiplierService& experience_multiplier,
                                  RuntimeBattleLevelSyncService& battle_level_sync,
                                  RuntimeControllerPreferencesService& settings,
                                  RuntimeGoaSettingsService& goa_settings,
                                  std::function<std::optional<std::filesystem::path>()> bridge_dir)
        -> RuntimeOptionsMenuDefinition
    {
        RuntimeOptionsMenuBuilder menu{STR("goa"), STR("AP Options"),
                                       STR("Open Archipelago gameplay and convenience settings.")};
        auto page = menu.page();
        character_swap::append_menu(page);
        add_deathlink_options(page, std::move(bridge_dir));
        page.toggle(
            STR("auto_equip_movement_abilities"), STR("Auto Equip Movement Abilities"),
            [&goa_settings]()
            {
                return goa_settings.auto_equip_movement_abilities();
            },
            [&goa_settings](bool enabled)
            {
                (void)goa_settings.set_auto_equip_movement_abilities(enabled);
            },
            {},
            []()
            {
                return std::wstring{STR("Equip received movement abilities when enough AP is free")};
            });
        page.toggle(
            STR("background_controller_input"), STR("Background Controller Input"),
            [&settings]()
            {
                return settings.background_input_allowed();
            },
            [&settings](bool enabled)
            {
                (void)settings.set_background_input_allowed(enabled);
            },
            {},
            []()
            {
                return std::wstring{STR("ON allows controller input while the game is unfocused. "
                                        "OFF blocks background controller input.")};
            },
            []()
            {
                return std::wstring{STR("Global setting.")};
            });

        page.submenu(
            STR("map_options"), STR("Map options"),
            [&goa_settings](RuntimeOptionsPageBuilder& map)
            {
                map.toggle(
                    STR("song_display"), STR("Song Display"),
                    [&goa_settings]()
                    {
                        return goa_settings.song_display();
                    },
                    [&goa_settings](bool enabled)
                    {
                        (void)goa_settings.set_song_display(enabled);
                    },
                    {},
                    []()
                    {
                        return std::wstring{
                            STR("Show the current song at the top center.")};
                    },
                    []()
                    {
                        return std::wstring{STR("Changes apply immediately and "
                                                "are saved for this seed.")};
                    });
                map.toggle(
                    STR("hide_locked_chest_icons"), STR("Hide Locked Chest Icons"),
                    [&goa_settings]()
                    {
                        return !map_icon_enabled(goa_settings.map_icon_options(),
                                                 MapIconOption::LockedChests);
                    },
                    [&goa_settings](bool hidden)
                    {
                        (void)goa_settings.set_map_icon_option(MapIconOption::LockedChests,
                                                               !hidden);
                    },
                    {},
                    []()
                    {
                        return std::wstring{
                            STR("ON hides chest icons whose Keyblade is missing. OFF shows them at "
                                "70% opacity. Owned chest icons stay fully visible.")};
                    },
                    []()
                    {
                        return std::wstring{STR("OFF by default. L2+R2 together toggles this while "
                                                "the large map is open. Saved for this seed.")};
                    });

                map.toggle(
                    STR("map_overlay"), STR("Map Overlay Shortcut"),
                    [&goa_settings]()
                    {
                        return goa_settings.map_overlay_enabled();
                    },
                    [&goa_settings](bool enabled)
                    {
                        (void)goa_settings.set_map_overlay_enabled(enabled);
                    },
                    {},
                    []()
                    {
                        return std::wstring{
                            STR("ON lets L3 or F8 open the full-size area map. OFF disables the "
                                "map shortcut and closes the overlay.")};
                    },
                    []()
                    {
                        return std::wstring{STR("Enabled by default. Changes apply immediately and "
                                                "are saved for this seed.")};
                    });
                for (const auto& [option, label] :
                     std::array{std::pair{MapIconOption::Chests, STR("Chests")},
                                std::pair{MapIconOption::Emblems, STR("Lucky Emblems")},
                                std::pair{MapIconOption::Ingredients, STR("Ingredients")},
                                std::pair{MapIconOption::Minimap, STR("Minimap Icons")},
                                std::pair{MapIconOption::LargeMap, STR("Large Map Icons")}})
                {
                    map.toggle(
                        std::format(STR("map_icons_{}"), static_cast<uint32_t>(option)), label,
                        [&goa_settings, option]()
                        {
                            return map_icon_enabled(goa_settings.map_icon_options(), option);
                        },
                        [&goa_settings, option](bool enabled)
                        {
                            (void)goa_settings.set_map_icon_option(option, enabled);
                        },
                        {},
                        [option]()
                        {
                            return std::wstring{option == MapIconOption::Minimap ||
                                                        option == MapIconOption::LargeMap
                                                    ? STR("Show the selected collectible icons.")
                                                    : STR("Show this icon type on both the minimap "
                                                          "and the large map.")};
                        },
                        []()
                        {
                            return std::wstring{
                                STR("All icons start ON in each seed. Changes apply immediately "
                                    "and are saved for this seed.")};
                        });
                }
            },
            {},
            []()
            {
                return std::wstring{STR("Choose map icons, shortcuts and the song display. Icon "
                                        "types are shared by both maps.")};
            });

        page.toggle(
            STR("battle_sync"), STR("Battle Level Sync"),
            [&battle_level_sync]()
            {
                return battle_level_sync.status().enabled;
            },
            [&battle_level_sync](bool enabled)
            {
                (void)battle_level_sync.set_enabled(enabled);
            },
            {},
            [&battle_level_sync]()
            {
                return std::format(STR("Battle level sync is {}. When ON, Sora's level "
                                       "drives the world battle level using the configured "
                                       "offset."),
                                   battle_level_sync.status().enabled ? STR("ON") : STR("OFF"));
            },
            []()
            {
                return std::wstring{STR("Choose ON or OFF. Changes apply immediately.")};
            });

        std::vector<RuntimeOptionsChoice> offsets{};
        offsets.reserve(battle_level_offsets.size());
        for (const int32_t offset : battle_level_offsets)
        {
            offsets.push_back(RuntimeOptionsChoice{
                .id = std::format(STR("offset_{}"), offset),
                .label = format_battle_level_offset(offset),
            });
        }
        page.choice(
            STR("battle_offset"), STR("Battle Level"), std::move(offsets),
            [&battle_level_sync]()
            {
                return battle_level_offset_choice_index(battle_level_sync.status().offset);
            },
            [&battle_level_sync](std::size_t selected)
            {
                if (selected < battle_level_offsets.size())
                {
                    (void)battle_level_sync.set_offset(battle_level_offsets[selected]);
                }
            },
            {},
            [&battle_level_sync]()
            {
                return std::format(STR("Battle level offset is {}. It is applied when Battle "
                                       "Level Sync is ON; 0 is the default."),
                                   format_battle_level_offset(battle_level_sync.status().offset));
            },
            []()
            {
                return std::wstring{STR("Choose a battle level offset from -3 through +5, "
                                        "or +10. Changes apply immediately when Battle "
                                        "Level Sync is ON.")};
            });

        std::vector<RuntimeOptionsChoice> experience_multipliers{};
        experience_multipliers.reserve(runtime_experience_multiplier_half_steps.size());
        for (const int32_t half_steps : runtime_experience_multiplier_half_steps)
        {
            experience_multipliers.push_back(RuntimeOptionsChoice{
                .id = std::format(STR("exp_{}"), half_steps),
                .label = format_experience_multiplier(half_steps),
            });
        }
        page.choice(
            STR("exp_multiplier"), STR("EXP Multiplier"), std::move(experience_multipliers),
            [&experience_multiplier]()
            {
                return experience_multiplier_choice_index(
                    experience_multiplier.status().multiplier_half_steps);
            },
            [&experience_multiplier](std::size_t selected)
            {
                if (selected < runtime_experience_multiplier_half_steps.size())
                {
                    (void)experience_multiplier.set_multiplier_half_steps(
                        runtime_experience_multiplier_half_steps[selected]);
                }
            },
            {},
            [&experience_multiplier]()
            {
                const RuntimeExperienceMultiplierStatus status = experience_multiplier.status();
                const std::wstring_view apply_state =
                    status.applied ? STR("applied") : STR("unavailable for this game build");
                return std::format(STR("Enemy EXP is {} ({})."),
                                   format_experience_multiplier(status.multiplier_half_steps),
                                   apply_state);
            },
            []()
            {
                return std::wstring{STR("Choose an EXP multiplier from 0.5x through 20x.")};
            });

        return std::move(menu).build();
    }
} // namespace kh3ap
