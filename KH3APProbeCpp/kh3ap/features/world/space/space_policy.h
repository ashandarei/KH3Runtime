#pragma once

#include "kh3ap/features/kh02/core/kh02_host_world.h"
#include <array>
#include <optional>
#include <string_view>

namespace kh3ap::space
{
    // Native landing states are serialized integers owned by TresGame.
    inline constexpr int landing_locked = 0;
    inline constexpr int landing_first_visit = 1;
    inline constexpr int landing_revisit = 3;
    inline constexpr int landing_completed = 4;
    inline constexpr unsigned mission_count = 46;
    inline constexpr unsigned first_boss_mission_id = 7;
    inline constexpr unsigned last_boss_mission_id = 17;
    inline constexpr unsigned boss_mission_count = last_boss_mission_id - first_boss_mission_id + 1;
    inline constexpr unsigned completionist_mission_id = mission_count - 1;
    inline constexpr size_t maximum_mission_row_digits = 2;
    inline constexpr unsigned travel_point_kind = 4;
    inline constexpr unsigned world_symbol_kind = 3;
    inline constexpr unsigned travel_points_per_region = 3;
    inline constexpr int first_overview_travel_point = 15;
    inline constexpr unsigned first_region_world_count = 4;
    inline constexpr unsigned second_region_world_end = 8;

    constexpr unsigned world_symbol_zone(unsigned id)
    {
        return id < first_region_world_count ? 0 : id < second_region_world_end ? 1 : 2;
    }

    // Unknown Garden progress is not a first visit. Retail routing can send a
    // progressed world back into its introduction-only map while assets load.
    constexpr int landing_world_state(bool allowed, std::optional<bool> progressed, int previous)
    {
        if (!allowed || !progressed.has_value())
        {
            return landing_locked;
        }
        if (!*progressed)
        {
            return landing_first_visit;
        }
        return previous == landing_completed ? landing_completed : landing_revisit;
    }
    // Supported totals with optional grindy/superboss groups and their combined
    // Completionist check.
    inline constexpr std::array<size_t, 4> supported_reward_counts{79, 85, 86, 93};
    constexpr bool supported_reward_count(size_t count)
    {
        for (const size_t supported_count : supported_reward_counts)
        {
            if (count == supported_count)
            {
                return true;
            }
        }
        return false;
    }
    constexpr std::optional<unsigned> mission_row_id(std::wstring_view row)
    {
        if (row.empty() || row.size() > maximum_mission_row_digits)
        {
            return std::nullopt;
        }
        unsigned id{};
        for (const auto c : row)
        {
            if (c < L'0' || c > L'9')
            {
                return std::nullopt;
            }
            id = id * 10 + static_cast<unsigned>(c - L'0');
        }
        return id < mission_count ? std::optional{id} : std::nullopt;
    }
    constexpr bool mission_list_enabled(unsigned id, bool grindy, bool superbosses)
    {
        switch (id)
        {
        case 20:
        case 21:
        case 22:
        case 27:
        case 33:
        case 34:
        case 35:
            return grindy;
        case 13:
        case 17:
            return superbosses;
        case completionist_mission_id:
            return grindy && superbosses;
        default:
            return id < mission_count;
        }
    }
    // Cooked map names and table rows share case-insensitive Unreal FNames.
    // Their display casing can differ from the stable AP source IDs.
    constexpr bool battle_first_reward_matches(std::wstring_view observed, std::wstring_view source)
    {
        constexpr std::wstring_view first_reward_suffix = L"_1";
        if (observed.empty() || source.size() != observed.size() + first_reward_suffix.size() ||
            !source.ends_with(first_reward_suffix))
        {
            return false;
        }
        const auto lower = [](wchar_t character)
        {
            return character >= L'A' && character <= L'Z' ? character + (L'a' - L'A') : character;
        };
        for (size_t index = 0; index < observed.size(); ++index)
        {
            if (lower(observed[index]) != lower(source[index]))
            {
                return false;
            }
        }
        return true;
    }
    constexpr std::wstring_view battle_from_starter_path(std::wstring_view path)
    {
        constexpr std::wstring_view prefix = L"/Game/Levels/gm/";
        constexpr std::wstring_view battle_folder = L"/umap/Battle/";
        if (!path.starts_with(prefix))
        {
            return {};
        }
        const auto folder = path.find(battle_folder, prefix.size());
        if (folder == path.npos)
        {
            return {};
        }
        path.remove_prefix(folder + battle_folder.size());
        const auto dot = path.find(L'.');
        if (dot == path.npos || dot == 0 || path.find(L":PersistentLevel.", dot) == path.npos)
        {
            return {};
        }
        return path.substr(0, dot);
    }
    inline constexpr std::wstring_view world_map = L"/Game/Levels/wm/wm_01/wm_01";
    constexpr bool restore_story_encounter(bool access_ready, bool include_space,
                                           std::wstring_view world, std::wstring_view starter)
    {
        // Do not remove in-space encounter prerequisites (notably Omega's
        // Eclipse sequence), or alter vanilla seeds and preview/template actors.
        return access_ready && include_space &&
               world == L"World /Game/Levels/gm/gm_01/gm_01.gm_01" &&
               starter.starts_with(L"/Game/Levels/gm/gm_01/umap/Battle/") &&
               battle_first_reward_matches(battle_from_starter_path(starter), L"gm_01_Battle_10_1");
    }
    inline constexpr std::wstring_view garden = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01";
    inline constexpr std::wstring_view garden_start = L"NormalSpawn";
    constexpr bool camp_returns_to_space(std::wstring_view world)
    {
        return world == L"World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01";
    }
    inline constexpr std::wstring_view garden_display = L"rg_03_An_01";
    inline constexpr int garden_world_code = 24;
    constexpr bool is_heartbinder(std::wstring_view token)
    {
        return token == L"KEY_ITEM06" || token == L"KEY_ITEM07" || token == L"KEY_ITEM08" ||
               token == L"KEY_ITEM09" || token == L"KEY_ITEM10";
    }
    constexpr bool supports_inventory_grant(std::wstring_view method, std::wstring_view token = {})
    {
        // Heartbinders also register Sora Link commands; space has no Sora owner.
        return !is_heartbinder(token) &&
               (method == L"bp_tres_get_item" || method == L"bp_tres_get_item_add_munny" ||
                method == L"ultima_weapon_shard");
    }
    // The overview is the normal space interface. Other focused panels and
    // engine pause/loading gates must still hold inventory delivery.
    constexpr bool inventory_ui_allowed(bool active, std::wstring_view reason,
                                        std::wstring_view context)
    {
        return !active || (reason == L"pause_menu" && context == L"TresUIP_MenuGummiShip");
    }
    constexpr bool inventory_delivery_world(std::wstring_view world)
    {
        return world == L"World /Game/Levels/wm/wm_01/wm_01.wm_01" ||
               world == L"World /Game/Levels/gm/gm_01/gm_01.gm_01" ||
               world == L"World /Game/Levels/gm/gm_02/gm_02.gm_02" ||
               world == L"World /Game/Levels/gm/gm_03/gm_03.gm_03";
    }
    inline constexpr std::wstring_view entry_scene = L"/Game/Levels/sf/sf_50/sf_50";
    inline constexpr std::wstring_view gummi_scene = L"/Game/Levels/gm/gm_50/gm_50";
    inline constexpr std::wstring_view yen_sid_scene = L"/Game/Levels/yt/yt_50/yt_50";
    inline constexpr std::wstring_view tower_scene = L"/Game/Levels/rg/rg_51/rg_51";

    constexpr bool is_entry_scene(std::wstring_view source)
    {
        return source.find(entry_scene) != source.npos || source.find(tower_scene) != source.npos;
    }
    constexpr bool entry_scene_transition(bool journey, std::wstring_view source,
                                          std::wstring_view target)
    {
        return journey && (target == entry_scene ||
                           (source.find(entry_scene) != source.npos && target == tower_scene));
    }

    // ETresGumiShipTravelPointID: three real points per region; GM04 is unused.
    inline constexpr std::array<std::wstring_view, 3> travel_maps{L"/Game/Levels/gm/gm_01/gm_01",
                                                                  L"/Game/Levels/gm/gm_02/gm_02",
                                                                  L"/Game/Levels/gm/gm_03/gm_03"};
    constexpr int flight_zone(std::wstring_view map)
    {
        constexpr std::wstring_view world_prefix = L"World ";
        if (map.starts_with(world_prefix))
        {
            map.remove_prefix(world_prefix.size());
        }
        for (unsigned i = 0; i < travel_maps.size(); ++i)
        {
            const auto prefix = travel_maps[i].substr(0, travel_maps[i].rfind(L'/') + 1);
            if (map.starts_with(prefix))
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    inline constexpr std::array<std::wstring_view, 9> travel_tags{
        L"gm_01_TP_01", L"gm_01_TP_02", L"gm_01_TP_03", L"gm_02_TP_01", L"gm_02_TP_02",
        L"gm_02_TP_03", L"gm_03_TP_01", L"gm_03_TP_02", L"gm_03_TP_03"};
    constexpr bool unlock_travel_point(unsigned kind, unsigned id, std::wstring_view map,
                                       std::wstring_view tag)
    {
        return kind == travel_point_kind && id < travel_tags.size() &&
               map == travel_maps[id / travel_points_per_region] && tag == travel_tags[id];
    }
    // Retail starting-point actors may be active before their saved discovery.
    // Rearm only those actors; native proximity then owns the discovery event.
    constexpr bool rearm_initial_travel_point(bool ap_ready, unsigned id, std::wstring_view world,
                                              bool discovered)
    {
        return ap_ready && !discovered &&
               ((id == travel_points_per_region &&
                 world == L"World /Game/Levels/gm/gm_02/gm_02.gm_02") ||
                (id == 2 * travel_points_per_region &&
                 world == L"World /Game/Levels/gm/gm_03/gm_03.gm_03"));
    }
    // ETresGumiShipWorldSymbolID order, distinct from the overview cache order.
    inline constexpr std::array<std::wstring_view, 9> world_symbol_codes{
        L"he", L"tt", L"ts", L"ra", L"fz", L"mi", L"bx", L"ca", L"kg"};
    constexpr int landing_zone(std::wstring_view code)
    {
        for (unsigned i = 0; i < world_symbol_codes.size(); ++i)
        {
            if (world_symbol_codes[i] == code)
            {
                return static_cast<int>(world_symbol_zone(i));
            }
        }
        return -1;
    }
    inline constexpr std::array<std::wstring_view, 9> world_embark_tags{
        L"gm_01_WorldSymbol_HE", L"gm_01_WorldSymbol_TT", L"gm_01_WorldSymbol_TS",
        L"gm_01_WorldSymbol_RA", L"gm_02_WorldSymbol_FZ", L"gm_02_WorldSymbol_MI",
        L"gm_02_WorldSymbol_BX", L"gm_02_WorldSymbol_CA", L"gm_03_WorldSymbol_KG"};
    constexpr std::wstring_view world_embark_code(std::wstring_view map, std::wstring_view tag)
    {
        for (unsigned i = 0; i < world_embark_tags.size(); ++i)
        {
            if (tag == world_embark_tags[i] && map == travel_maps[world_symbol_zone(i)])
            {
                return world_symbol_codes[i];
            }
        }
        return {};
    }
    constexpr std::wstring_view world_symbol_code(unsigned kind, unsigned id)
    {
        return kind == world_symbol_kind && id < world_symbol_codes.size() ? world_symbol_codes[id]
                                                                           : std::wstring_view{};
    }

    constexpr int overview_default_fallback(unsigned region, unsigned kind, unsigned id,
                                            bool world_allowed)
    {
        // Overview object IDs for real travel points are 15..23, distinct
        // from the travel-point enum's 0..8. Preserve non-world selections.
        if (region >= travel_maps.size() || world_symbol_code(kind, id).empty())
        {
            return -1;
        }
        const unsigned world_region = world_symbol_zone(id);
        return world_allowed && world_region == region
                   ? -1
                   : first_overview_travel_point +
                         static_cast<int>(travel_points_per_region * region);
    }

    constexpr bool is_space(std::wstring_view source)
    {
        return source.find(L"/Game/Levels/wm/") != source.npos ||
               source.find(L"/Game/Levels/gm/") != source.npos;
    }

    // Classify the CURRENT world, never the previous playable Sora snapshot:
    // that snapshot deliberately remains unavailable while flying. Unknown
    // contexts and title/load paths are not evidence of an ordinary return.
    constexpr bool is_ordinary_world(std::wstring_view source)
    {
        return source.find(L"/Game/Levels/") != source.npos && !is_space(source);
    }

    constexpr bool redirect_to_garden(std::wstring_view source, std::wstring_view target,
                                      bool space_journey = false)
    {
        // World endings target scene-only SF50, GM50, or YT50. Startup has already
        // completed the forest scenes. GM50 is a cinematic map, not a flight zone;
        // passing it to the ship guard rejects the jump after the story fade.
        // YT50 can run its first scene and directly grant Dream Heartbinder
        // outside the AP receipt path when its story flag is still initial.
        // Route ordinary-world jumps and checkpoints straight to Garden.
        // Explicit space trips retain their interlude/tower continuation.
        const bool hub_target = target == world_map || target == L"/Game/Levels/gm/gm_01/gm_01" ||
                                target == L"/Game/Levels/gm/gm_02/gm_02" ||
                                target == L"/Game/Levels/gm/gm_03/gm_03" || target == entry_scene ||
                                target == gummi_scene || target == yen_sid_scene;
        // Imported 0.2 save points use the same native hub destination, but
        // their host maps live under /Game/Maps rather than /Game/Levels.
        return hub_target && (is_ordinary_world(source) || is_kh02_host_world(source)) &&
               !(space_journey && is_entry_scene(source));
    }

    struct Landing
    {
        std::wstring_view code;
        int world_code;
        std::wstring_view keyblade;
    };
    inline constexpr std::array landings{
        Landing{L"he", 4, L"Hero's Origin"},  Landing{L"ts", 5, L"Favorite Deputy"},
        Landing{L"ra", 7, L"Ever After"},     Landing{L"fz", 8, L"Crystal Snow"},
        Landing{L"ca", 9, L"Wheel of Fate"},  Landing{L"mi", 11, L"Happy Gear"},
        Landing{L"tt", 12, L"Shooting Star"}, Landing{L"kg", 14, L"Starlight"},
        Landing{L"bx", 19, L"Nano Gear"},     Landing{L"dw", 3, L"Oblivion"},
        Landing{L"rg", 24, L"Oathkeeper"},    Landing{L"di", 23, L"Kingdom Key"},
    };

    struct EmbarkDestination
    {
        std::wstring_view code;
        std::wstring_view map;
        std::wstring_view tag;
        unsigned symbol_id{};
    };
    constexpr EmbarkDestination first_embark_destination(int world_code)
    {
        // Vanilla first-visit starts can be beside a DIFFERENT world (BX at
        // MI, CA at FZ, TS/RA at TT). AP ownership has no such route ordering.
        for (const auto& world : landings)
        {
            if (world.world_code == world_code)
            {
                for (unsigned i = 0; i < world_symbol_codes.size(); ++i)
                {
                    if (world_symbol_codes[i] == world.code)
                    {
                        return {world.code, travel_maps[world_symbol_zone(i)], world_embark_tags[i],
                                i};
                    }
                }
            }
        }
        return {};
    }

    constexpr std::wstring_view destination_code(std::wstring_view target)
    {
        constexpr std::wstring_view prefix = L"/Game/Levels/";
        if (!target.starts_with(prefix))
        {
            return {};
        }
        target.remove_prefix(prefix.size());
        return target.substr(0, target.find(L'/'));
    }

    constexpr bool destination_requires_keyblade(std::wstring_view target)
    {
        // The standalone cooking menu shares TT's package directory, but
        // Visit the Bistro is a service, not access to Twilight Town.
        if (target == L"/Game/Levels/tt/tt_40/tt_40")
        {
            return false;
        }
        const auto code = destination_code(target);
        return !code.empty() && code != L"wm" && code != L"gm";
    }

    template <class HasItem> bool landing_allowed(std::wstring_view code, HasItem has_item)
    {
        // Physical world access requires only its Keyblade. Combat capability
        // and progression requirements belong to Archipelago location logic.
        for (const auto& candidate : landings)
        {
            if (candidate.code == code)
            {
                return has_item(candidate.keyblade);
            }
        }
        return false;
    }
} // namespace kh3ap::space
