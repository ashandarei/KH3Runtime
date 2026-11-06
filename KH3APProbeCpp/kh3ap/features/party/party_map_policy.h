#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include "kh3ap/features/party/party_navigation_maps.h"

namespace kh3ap
{
    struct RuntimePartyNativeCompanions
    {
        std::wstring_view world_path;
        std::wstring_view first;
        std::wstring_view second{};
    };

    inline constexpr std::array runtime_party_native_companions{
        RuntimePartyNativeCompanions{L"/Game/Levels/he/", L"hercules"},
        RuntimePartyNativeCompanions{L"/Game/Levels/ts/", L"woody", L"buzz"},
        RuntimePartyNativeCompanions{L"/Game/Levels/ra/", L"rapunzel", L"flynn"},
        RuntimePartyNativeCompanions{L"/Game/Levels/mi/", L"sulley", L"mike"},
        RuntimePartyNativeCompanions{L"/Game/Levels/ca/", L"jack"},
        RuntimePartyNativeCompanions{L"/Game/Levels/bx/", L"baymax"},
        RuntimePartyNativeCompanions{L"/Game/Levels/fz/", L"marshmallow"},
    };

    [[nodiscard]] constexpr auto
    runtime_party_is_ship_control_state(std::wstring_view level_name,
                                        std::wstring_view sora_state_class,
                                        bool already_at_helm = false) -> bool
    {
        // Walking on deck and exploring ashore retain the saved party;
        // other vehicles and attractions are unaffected.
        // Ship scenes can temporarily put the same Sora in Cinematic. Keep
        // the helm policy through those scenes until ordinary gameplay resumes.
        return level_name.find(L"/Game/Levels/ca/") != std::wstring_view::npos &&
               (sora_state_class == L"TresPlayerState_SpecialShip" ||
                (already_at_helm && sora_state_class == L"TresCharState_Cinematic"));
    }

    [[nodiscard]] constexpr auto
    runtime_party_is_native_world_companion(std::wstring_view level_name,
                                            std::wstring_view character) -> bool
    {
        // Friends 3/4 belong to the world. Exclude their identities even in
        // rooms/story phases where they have not spawned yet.
        for (const auto& companions : runtime_party_native_companions)
        {
            if (level_name.find(companions.world_path) != std::wstring_view::npos)
            {
                return character == companions.first ||
                       (!companions.second.empty() && character == companions.second);
            }
        }
        return false;
    }

    [[nodiscard]] constexpr auto runtime_party_is_character_disabled(std::wstring_view level_name,
                                                                     std::wstring_view character)
        -> bool
    {
        // Marshmallow may join elsewhere, but must never be selected/spawned
        // in the garage. Its existing vanilla-party policy remains authoritative.
        return character == L"marshmallow" &&
               level_name.find(L"/Game/Levels/bx/bx_03/") != std::wstring_view::npos;
    }

    [[nodiscard]] constexpr auto
    runtime_party_requires_entry_reconcile(std::wstring_view level_name) -> bool
    {
        return level_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01:") !=
               std::wstring_view::npos;
    }

    struct RuntimePartyNavigationFallback
    {
        std::wstring_view mesh_name;
        float radius{};
        float height{};
    };

    inline constexpr int runtime_party_medium_navigation_group = 2;
    inline constexpr int runtime_party_large_navigation_group = 3;
    inline constexpr RuntimePartyNavigationFallback runtime_party_normal_navigation{
        L"RecastNavMesh-Group1", 71.0f, 180.0f};
    inline constexpr RuntimePartyNavigationFallback runtime_party_medium_navigation{
        L"RecastNavMesh-Group2", 71.0f, 270.0f};
    inline constexpr RuntimePartyNavigationFallback runtime_party_large_navigation{
        L"RecastNavMesh-Group3", 114.0f, 280.0f};

    [[nodiscard]] constexpr auto runtime_party_navigation_level_matches(std::wstring_view actual,
                                                                        std::wstring_view expected)
        -> bool
    {
        // Unreal FNames preserve the first loaded spelling. FZ_03/FZ_04
        // therefore vary in case between live sessions and package exports.
        if (actual.size() != expected.size())
        {
            return false;
        }
        const auto lower = [](wchar_t character)
        {
            return character >= L'A' && character <= L'Z' ? character + (L'a' - L'A') : character;
        };
        for (std::size_t index = 0; index < actual.size(); ++index)
        {
            if (lower(actual[index]) != lower(expected[index]))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] constexpr auto
    runtime_party_navigation_fallback(std::wstring_view navigation_name,
                                      std::wstring_view level_name)
        -> RuntimePartyNavigationFallback
    {
        if (navigation_name == runtime_party_large_navigation.mesh_name)
        {
            for (const auto& map : runtime_party_large_navigation_maps)
            {
                if (runtime_party_navigation_level_matches(level_name, map.level))
                {
                    return map.group == runtime_party_medium_navigation_group
                               ? runtime_party_medium_navigation
                               : runtime_party_normal_navigation;
                }
            }
            return {};
        }
        if (navigation_name != runtime_party_medium_navigation.mesh_name)
        {
            return {};
        }
        for (const auto& map : runtime_party_navigation_maps)
        {
            if (runtime_party_navigation_level_matches(level_name, map.level))
            {
                return map.group == runtime_party_large_navigation_group
                           ? runtime_party_large_navigation
                           : runtime_party_normal_navigation;
            }
        }
        return {};
    }

    [[nodiscard]] constexpr auto
    runtime_party_requires_navigation_fallback(std::wstring_view level_name) -> bool
    {
        return !runtime_party_navigation_fallback(runtime_party_medium_navigation.mesh_name,
                                                  level_name)
                    .mesh_name.empty() ||
               !runtime_party_navigation_fallback(runtime_party_large_navigation.mesh_name,
                                                  level_name)
                    .mesh_name.empty();
    }

    [[nodiscard]] constexpr auto
    runtime_party_requires_context_reconcile(std::wstring_view level_name) -> bool
    {
        // King of Toys can omit SetFriendAll and a map-name event.
        return runtime_party_navigation_level_matches(
            level_name, L"Level /Game/Levels/ts/ts_04/ts_04.TS_04:PersistentLevel");
    }

    [[nodiscard]] constexpr auto runtime_party_is_data_battle_level(std::wstring_view level_name)
        -> bool
    {
        constexpr std::wstring_view prefix = L"/Game/Levels/rg_DLC/rg_";
        constexpr std::size_t room_number_digits = 2;
        constexpr unsigned first_data_battle_room = 10;
        constexpr unsigned last_data_battle_room = 22;
        const auto position = level_name.find(prefix);
        if (position == std::wstring_view::npos)
        {
            return false;
        }
        const auto room_path = level_name.substr(position + prefix.size());
        if (room_path.size() <= room_number_digits || room_path[room_number_digits] != L'/')
        {
            return false;
        }
        unsigned room_number{};
        for (const wchar_t digit : room_path.substr(0, room_number_digits))
        {
            if (digit < L'0' || digit > L'9')
            {
                return false;
            }
            room_number = room_number * 10 + static_cast<unsigned>(digit - L'0');
        }
        return room_number >= first_data_battle_room && room_number <= last_data_battle_room;
    }

    enum class RuntimePartyMapPolicy
    {
        managed,
        solo,
        vanilla,
        vanilla_first_only,
    };

    inline constexpr std::size_t runtime_party_vanilla_choice = 26;
    inline constexpr int runtime_party_dark_baymax_start = 710;
    inline constexpr int runtime_party_dark_baymax_end = 900;

    inline constexpr std::wstring_view runtime_party_flash_tracer_course_b_asset =
        L"/Game/Levels/bx/bx_02/umap/bx_02_FlashTracer_Minigame02.bx_02_FlashTracer_Minigame02";

    [[nodiscard]] constexpr auto
    runtime_party_is_flash_tracer_course_b_asset(std::wstring_view asset_path) -> bool
    {
        return runtime_party_navigation_level_matches(asset_path,
                                                      runtime_party_flash_tracer_course_b_asset);
    }

    [[nodiscard]] constexpr auto runtime_party_ship_policy(bool at_helm, bool randomized,
                                                           int first_choice, int second_choice)
        -> RuntimePartyMapPolicy
    {
        if (!at_helm)
        {
            return RuntimePartyMapPolicy::managed;
        }
        // Preserve the existing stock crew, including their native ship
        // attachment. Customized or randomized friends stay absent at the
        // helm; spawning pirate replacements misses that attachment step.
        return !randomized && first_choice == static_cast<int>(runtime_party_vanilla_choice) &&
                       second_choice == static_cast<int>(runtime_party_vanilla_choice)
                   ? RuntimePartyMapPolicy::vanilla
                   : RuntimePartyMapPolicy::solo;
    }

    // These maps own their encounter roster on entry and Retry. Saved manual
    // choices resume after leaving; removing story actors can stall native tasks.
    inline constexpr std::array<std::wstring_view, 8> runtime_party_vanilla_level_fragments{
        L"/Game/Levels/he/he_04/",     // Cherry Flan needs Donald/Goofy before shield-ride setup.
        L"/Game/Levels/fz/fz_03/",     // Frozen Slider needs the native pair before its countdown.
        L"/Game/Levels/bt/bt_03/",     // Underwater Armored Xehanort needs swimming companions.
        L"/Game/Levels/bx/bx_03/",     // Hiro's Garage scenes own their story party.
        L"/Game/Levels/kg_DLC/kg_06/", // Guardians/final Xehanort own Kairi and character
                                       // selection.
        L"/Game/Levels/kg/kg_02/",     // Base Skein arenas own their allies and playable selection.
        L"/Game/Levels/kg_DLC/kg_05/", // Re Mind Skein arenas follow the same ownership rule.
        L"/Game/Levels/kg_DLC/kg_04/kg_05.", // Streamed Re Mind arena under its parent package.
    };

    inline constexpr std::array<std::wstring_view, 8> runtime_party_solo_level_fragments{
        L"/Game/Levels/ew/",
        L"/Game/Levels/bt_DLC/", // Re Mind Scala.
        L"/Game/Levels/kg_DLC/kg_04/",
        L"/Game/Levels/kg_DLC/kg_08/", // Re Mind Terra-Xehanort.
        L"/Game/Levels/ss_DLC/ss_01/", // Yozora.
        L"/Game/Levels/ts/ts_03/",     // Verum Rex.
        L"/Game/Levels/dw/dw_21/",     // Dark Aqua.
        L"/Game/Levels/ex/ex_38/",     // Dark Inferno chi.
    };

    [[nodiscard]] constexpr auto
    runtime_party_map_policy(std::wstring_view level_name, int kg_dlc_progress = -1,
                             int bx_progress = -1, bool bx_flash_tracer_course_b = false)
        -> RuntimePartyMapPolicy
    {
        for (const auto fragment : runtime_party_vanilla_level_fragments)
        {
            if (level_name.find(fragment) != std::wstring_view::npos)
            {
                return RuntimePartyMapPolicy::vanilla;
            }
        }

        // Go Go's Course B owns its companions during intro, race and results.
        // Its stream is requested before the first SetFriendAll, even after
        // story completion. Unloading it resumes ordinary city exploration.
        if (level_name.find(L"/Game/Levels/bx/bx_02/") != std::wstring_view::npos &&
            (bx_flash_tracer_course_b ||
             level_name.find(L"/umap/bx_02_FlashTracer_Minigame02.") != std::wstring_view::npos))
        {
            return RuntimePartyMapPolicy::vanilla;
        }
        // Dark Baymax owns the vehicle party through preparation, both battle
        // phases and the ending. Darkubes and ordinary city exploration remain managed.
        if (level_name.find(L"/Game/Levels/bx/bx_02/") != std::wstring_view::npos &&
            bx_progress >= runtime_party_dark_baymax_start &&
            bx_progress < runtime_party_dark_baymax_end)
        {
            return RuntimePartyMapPolicy::vanilla;
        }

        // Preserve solo encounter ownership regardless of saved party choices.
        for (const auto fragment : runtime_party_solo_level_fragments)
        {
            if (level_name.find(fragment) != std::wstring_view::npos)
            {
                return RuntimePartyMapPolicy::solo;
            }
        }
        if (runtime_party_is_data_battle_level(level_name))
        {
            return RuntimePartyMapPolicy::solo;
        }
        return RuntimePartyMapPolicy::managed;
    }
} // namespace kh3ap
