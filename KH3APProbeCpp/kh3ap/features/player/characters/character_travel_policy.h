#pragma once
#include <cstddef>
#include <string_view>

namespace kh3ap::character_swap
{
    constexpr bool can_prepare_arrival_under_cover(std::wstring_view world,
            bool changed_world, bool loading_cover_open)
    {
        // Ordinary ground-map arrivals can restore the player before the reveal.
        // Caribbean arrivals use this path too; scripted boarding prepares the
        // retained hero before the vehicle captures its player. Space, Re Mind's
        // scripted ownership and same-world handoffs retain their readiness gate.
        // Live possession, KH0.2 and cinematic ownership checks remain at the call site.
        const bool ground_map = world.starts_with(L"/Game/Levels/") &&
            !world.starts_with(L"/Game/Levels/wm/") &&
            !world.starts_with(L"/Game/Levels/gm/") &&
            !world.starts_with(L"/Game/Levels/kg_DLC/");
        const bool garden = world == L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01";
        return changed_world && loading_cover_open && (ground_map || garden);
    }

    constexpr bool travel_adoption_is_current(bool same_pawn, bool same_world,
            int active_character, int travel_character)
    {
        return same_pawn && same_world && travel_character >= 0 &&
               active_character == travel_character;
    }

    constexpr bool is_olympus_collapse_scenery(std::wstring_view world, std::wstring_view matinee)
    {
        return world == L"/Game/Levels/he/he_03/he_03.he_03" &&
               (matinee == L"/Game/Levels/he/he_03/umap/he_03_camera_ms024s."
                           L"he_03_camera_ms024s:PersistentLevel.ms024s_01" ||
                matinee == L"/Game/Levels/he/he_03/umap/he_03_camera_ms024s."
                           L"he_03_camera_ms024s:PersistentLevel.ms024s_02");
    }

    constexpr bool collapse_sora_role_is_inactive(std::wstring_view world,
            std::wstring_view matinee, size_t track_count, size_t disabled_track_count)
    {
        // These scenery timelines retain an authored Sora role whose tracks
        // are disabled. Its mere presence must not replace a playable ally.
        // Unknown/empty layouts and any enabled track still require the normal
        // cinematic ownership path.
        return track_count > 0 && track_count == disabled_track_count &&
               is_olympus_collapse_scenery(world, matinee);
    }

    constexpr bool is_tornado_freefall_cinematic(std::wstring_view world, std::wstring_view matinee)
    {
        return world.starts_with(L"/Game/Levels/he/he_06/he_06.") &&
               matinee == L"/Game/Levels/he/he_06/umap/he_06_camera_ms130."
                          L"he_06_camera_ms130:PersistentLevel.ms130m";
    }

    constexpr bool is_remind_maze_exit_cinematic(std::wstring_view world, std::wstring_view matinee)
    {
        return world == L"/Game/Levels/kg_DLC/kg_05/kg_05.kg_05" &&
               (matinee == L"/Game/Levels/kg/kg_02/umap/kg_02_camera_ms081."
                           L"kg_02_camera_ms081:PersistentLevel.ms081_C-D" ||
                matinee == L"/Game/Levels/kg/kg_02/umap/kg_02_camera_ms091."
                           L"kg_02_camera_ms091:PersistentLevel.ms091_D-E");
    }

    constexpr int travel_target_after_swap(int retained, int incoming, int sora, bool cinematic)
    {
        return cinematic ? retained : incoming == sora ? -1 : incoming;
    }
}
