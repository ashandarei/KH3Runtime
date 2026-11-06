#include <optional>
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/ui/goa_options_menu.h"
#include "kh3ap/core/settings/seed_ui_options.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"

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
        constexpr auto mom_style_field = 0;
        constexpr auto mom_item_field = 1;
        constexpr auto mom_party_field = 2;
        constexpr auto mom_difficulty_field = 3;
        constexpr auto mom_difficulty_choice_count = 3;
        constexpr auto mom_last_difficulty_choice = mom_difficulty_choice_count - 1;
    } // namespace

    auto
    make_mom_runtime_options_menu(std::function<std::optional<std::filesystem::path>()> bridge_dir)
        -> RuntimeOptionsMenuDefinition
    {
        // This sidecar is shared with the client on Windows and Linux. Read
        // only for menu interaction; no per-frame filesystem polling.
        const auto read = [bridge_dir]()
        {
            auto values = read_runtime_ui_seed_defaults(installed_seed_slot_data()).mom;
            if (const auto dir = bridge_dir())
            {
                if (const auto text = read_text_file(*dir / STR("mom_settings.json")))
                {
                    if (JsonFieldReader::int64_field(*text, "schema_version") == 1)
                    {
                        const auto style = JsonFieldReader::int64_field(*text, "play_style");
                        const auto item = JsonFieldReader::int64_field(*text, "starting_item");
                        const auto party = JsonFieldReader::int64_field(*text, "party");
                        const auto difficulty = JsonFieldReader::int64_field(*text, "difficulty");
                        if (style && *style >= 0 &&
                            *style <= seed_ui_limits::maximum_mom_play_style)
                        {
                            values[mom_style_field] = *style;
                        }
                        if (item && *item >= 0 &&
                            *item <= seed_ui_limits::maximum_mom_starting_item)
                        {
                            values[mom_item_field] = *item;
                        }
                        if (party && *party >= 0 && *party <= seed_ui_limits::maximum_mom_party)
                        {
                            values[mom_party_field] = *party;
                        }
                        if (difficulty && *difficulty >= 0 &&
                            *difficulty <= mom_difficulty_choice_count)
                        {
                            values[mom_difficulty_field] = *difficulty;
                        }
                    }
                }
            }
            return values;
        };
        const auto write = [bridge_dir, read](std::size_t field, std::size_t value)
        {
            if (field > mom_difficulty_field ||
                value > static_cast<std::size_t>(field == mom_style_field
                                                     ? seed_ui_limits::maximum_mom_play_style
                                                     : mom_difficulty_choice_count))
            {
                return;
            }
            const auto dir = bridge_dir();
            if (!dir)
            {
                return;
            }
            auto values = read();
            values[field] = value;
            (void)write_text_file_atomic_verified(
                *dir / STR("mom_settings.json"),
                std::format("{{\"schema_version\":1,\"play_style\":{},\"starting_item\":{},"
                            "\"party\":{},\"difficulty\":{}}}\n",
                            values[mom_style_field], values[mom_item_field],
                            values[mom_party_field], values[mom_difficulty_field]));
        };
        const auto available = [bridge_dir]()
        {
            return bridge_dir().has_value();
        };
        RuntimeOptionsMenuBuilder menu{STR("melody_of_memory"), STR("Melody of Memory"),
                                       STR("Choose the party, difficulty, play style and item for "
                                           "the next Melody of Memory song launch.")};
        const auto party_help = []()
        {
            return std::wstring{
                STR("Classic: Sora, Donald and Goofy. Days: Roxas, Axel and Xion. "
                    "3D: Riku, Meow Wow and Komory Bat. BBS: Aqua, Terra and Ventus.")};
        };
        menu.page().choice(
            STR("mom_party"), STR("Party"),
            {{STR("classic"), STR("Team Classic")},
             {STR("days"), STR("Team Days")},
             {STR("3d"), STR("Team 3D")},
             {STR("bbs"), STR("Team BBS")}},
            [read]()
            {
                return read()[mom_party_field];
            },
            [write](std::size_t value)
            {
                write(mom_party_field, value);
            },
            available, party_help, party_help);
        const auto difficulty_help = []()
        {
            return std::wstring{STR("Clearing the selected difficulty completes the challenge.")};
        };
        menu.page().choice(
            STR("mom_difficulty"), STR("Difficulty"),
            {{STR("beginner"), STR("Beginner")},
             {STR("standard"), STR("Standard")},
             {STR("proud"), STR("Proud")}},
            [read]() -> std::size_t
            {
                // The sidecar uses zero for an unset preference and 1-3 for
                // explicit choices. Display the generated difficulty when unset.
                if (const auto value = read()[mom_difficulty_field]; value != 0)
                {
                    return value - 1;
                }
                return static_cast<std::size_t>(seed_choice_option(installed_seed_slot_data(),
                                                                   "melody_of_memory_difficulty",
                                                                   mom_last_difficulty_choice));
            },
            [write](std::size_t value)
            {
                if (value < mom_difficulty_choice_count)
                {
                    write(mom_difficulty_field, value + 1);
                }
            },
            available, difficulty_help, difficulty_help);
        const auto style_help = []()
        {
            return std::wstring{STR("All three styles can complete a song challenge.")};
        };
        menu.page().choice(
            STR("mom_style"), STR("Play Style"),
            {{STR("normal"), STR("Normal")},
             {STR("one_button"), STR("One Button")},
             {STR("performer"), STR("Performer")}},
            [read]()
            {
                return read()[mom_style_field];
            },
            [write](std::size_t value)
            {
                write(mom_style_field, value);
            },
            available, style_help, style_help);
        const auto item_help = []()
        {
            return std::wstring{STR("None: No item. Potion: Restores 30% HP at 40% HP or below. "
                                    "Mega-Potion: Restores 60% HP at 20% HP or below. "
                                    "Summoning Star: Summons King Mickey to help.")};
        };
        menu.page().choice(
            STR("mom_item"), STR("Starting Item"),
            {{STR("none"), STR("None")},
             {STR("potion"), STR("Potion")},
             {STR("mega_potion"), STR("Mega-Potion")},
             {STR("summoning_star"), STR("Summoning Star")}},
            [read]()
            {
                return read()[mom_item_field];
            },
            [write](std::size_t value)
            {
                write(mom_item_field, value);
            },
            available, item_help, item_help);
        return std::move(menu).build();
    }
} // namespace kh3ap
