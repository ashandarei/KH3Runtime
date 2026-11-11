#include "kh3ap/features/party/party_map_policy.h"
#include <cassert>
#include <string>
#include <utility>

namespace
{
    using namespace kh3ap;

    void test_flash_tracer_context()
    {
        assert(runtime_party_is_flash_tracer_course_b_asset(
            L"/Game/Levels/bx/bx_02/umap/"
            L"bx_02_FlashTracer_Minigame02.bx_02_FlashTracer_Minigame02"));
        assert(runtime_party_is_flash_tracer_course_b_asset(
            L"/game/levels/bx/bx_02/umap/"
            L"BX_02_FLASHTRACER_MINIGAME02.bx_02_flashtracer_minigame02"));
        for (auto other :
             {L"/Game/Levels/bx/bx_02/umap/bx_02_FlashTracer_Main.bx_02_FlashTracer_Main",
              L"/Game/Levels/bx/bx_02/umap/"
              L"bx_02_FlashTracer_Minigame02_enemy06.bx_02_FlashTracer_Minigame02_enemy06",
              L"/Game/Levels/bx/bx_02/umap/"
              L"bx_02_FlashTracer_Minigame020.bx_02_FlashTracer_Minigame020"})
        {
            assert(!runtime_party_is_flash_tracer_course_b_asset(other));
        }
        constexpr auto city = L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel";
        // Loading/intro, race, results, then course unload in the same city world.
        // Both a finished story and early story state preserve the course's roster.
        for (auto story : {-1, 0, 710, 899, 900, 9999})
        {
            for (bool requested : {true, true, true, false})
            {
                assert(runtime_party_map_policy(city, -1, story, requested) ==
                       (requested || (story >= 710 && story < 900)
                            ? RuntimePartyMapPolicy::vanilla
                            : RuntimePartyMapPolicy::managed));
            }
        }
        assert(runtime_party_map_policy(
                   L"Level "
                   L"/Game/Levels/bx/bx_02/umap/"
                   L"bx_02_FlashTracer_Minigame02.bx_02_FlashTracer_Minigame02:PersistentLevel",
                   -1, 9999) == RuntimePartyMapPolicy::vanilla);
        assert(runtime_party_map_policy(
                   L"Level "
                   L"/Game/Levels/bx/bx_02/umap/"
                   L"bx_02_FlashTracer_Main.bx_02_FlashTracer_Main:PersistentLevel",
                   -1, 9999) == RuntimePartyMapPolicy::managed);
        assert(runtime_party_map_policy(L"Level /Game/Levels/ts/ts_02/ts_02.ts_02:PersistentLevel",
                                        -1, 9999, true) == RuntimePartyMapPolicy::managed);
    }

    void test_ship_control_context()
    {
        for (auto level :
             {L"Level /Game/Levels/ca/ca_02/ca_02.ca_02:PersistentLevel",
              L"Level /Game/Levels/ca/ca_02/umap/ca_02_gameflow.ca_02_gameflow:PersistentLevel"})
        {
            assert(runtime_party_is_ship_control_state(level, L"TresPlayerState_SpecialShip"));
            for (auto state :
                 {L"TresPlayerState_Idle", L"TresPlayerState_Run", L"TresPlayerState_Swim",
                  L"TresCharState_Cinematic", L"TresPlayerState_AFVikingShip"})
            {
                assert(!runtime_party_is_ship_control_state(level, state));
            }
            // Caribbean exploration still permits the selected companions.
            assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
        }
        assert(!runtime_party_is_ship_control_state(
            L"Level /Game/Levels/ts/ts_02/ts_02.ts_02:PersistentLevel",
            L"TresPlayerState_SpecialShip"));
        constexpr auto caribbean = L"Level /Game/Levels/ca/ca_02/ca_02.ca_02:PersistentLevel";
        bool at_helm = false;
        for (auto state : {L"TresPlayerState_SpecialShip", L"TresCharState_Cinematic",
                           L"TresPlayerState_SpecialShip"})
        {
            at_helm = runtime_party_is_ship_control_state(caribbean, state, at_helm);
            assert(at_helm); // Taking the helm, ship scene, returning to helm.
            assert(runtime_party_ship_policy(at_helm, false, 15, 16) ==
                   RuntimePartyMapPolicy::solo);
            assert(runtime_party_ship_policy(at_helm, false, 26, 26) ==
                   RuntimePartyMapPolicy::vanilla);
        }
        at_helm = runtime_party_is_ship_control_state(caribbean, L"TresPlayerState_Idle", at_helm);
        assert(!at_helm); // Leaving the controls restores the selected party.
        assert(runtime_party_ship_policy(at_helm, false, 15, 16) == RuntimePartyMapPolicy::managed);
        assert(runtime_party_ship_policy(at_helm, true, 26, 26) == RuntimePartyMapPolicy::managed);
        assert(
            !runtime_party_is_ship_control_state(caribbean, L"TresCharState_Cinematic", at_helm));
        assert(!runtime_party_is_ship_control_state(
            L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel", L"TresCharState_Cinematic",
            true));
        // Randomization overrides saved Vanilla selections. Mixed Vanilla/custom,
        // ordinary Donald/Goofy, explicit pirate costumes and None all stay absent.
        assert(runtime_party_ship_policy(true, true, 26, 26) == RuntimePartyMapPolicy::solo);
        for (auto pair : {std::pair{26, 15}, std::pair{15, 26}, std::pair{1, 2}, std::pair{20, 21},
                          std::pair{0, 0}})
        {
            assert(runtime_party_ship_policy(true, false, pair.first, pair.second) ==
                   RuntimePartyMapPolicy::solo);
        }
    }

    void test_story_map_boundaries()
    {
        // The reported Retry at 22100 spawns Kairi in friend index 1. Preserve
        // both native slots through selection and every later fight phase.
        for (int progress : {0, 22099, 22100, 22200, 22250, 99999})
        {
            for (auto level : {L"Level /Game/Levels/kg_DLC/kg_06/kg_06.kg_06:PersistentLevel",
                               L"Level "
                               L"/Game/Levels/kg_DLC/kg_06/umap/"
                               L"kg_06_camera_ms160.kg_06_camera_ms160:PersistentLevel"})
            {
                assert(runtime_party_map_policy(level, progress) == RuntimePartyMapPolicy::vanilla);
            }
        }
        for (auto level :
             {L"Level /Game/Levels/he/he_04/he_04.HE_04:PersistentLevel",
              L"Level /Game/Levels/he/he_04/umap/he_04_gameflow.he_04_gameflow:PersistentLevel",
              L"Level /Game/Levels/bx/bx_03/bx_03.bx_03:PersistentLevel",
              L"Level /Game/Levels/bx/bx_03/umap/bx_03_gameflow.bx_03_gameflow:PersistentLevel",
              L"Level /Game/Levels/kg/kg_02/kg_02.kg_02:PersistentLevel",
              L"Level "
              L"/Game/Levels/kg/kg_02/umap/kg_02_gameflow_Main.kg_02_gameflow_Main:PersistentLevel",
              L"Level /Game/Levels/kg_DLC/kg_05/kg_05.kg_05:PersistentLevel",
              L"Level /Game/Levels/kg_DLC/kg_04/kg_05.kg_05:PersistentLevel",
              L"Level "
              L"/Game/Levels/kg_DLC/kg_05/umap/"
              L"kg_05_enemy_Area_A.kg_05_enemy_Area_A:PersistentLevel",
              L"Level "
              L"/Game/Levels/kg_DLC/kg_05/umap/"
              L"kg_05_enemy_Area_B.kg_05_enemy_Area_B:PersistentLevel",
              L"Level "
              L"/Game/Levels/kg_DLC/kg_05/umap/"
              L"kg_05_enemy_Area_C.kg_05_enemy_Area_C:PersistentLevel",
              L"Level "
              L"/Game/Levels/kg_DLC/kg_05/umap/"
              L"kg_05_enemy_Area_D.kg_05_enemy_Area_D:PersistentLevel",
              L"Level "
              L"/Game/Levels/kg_DLC/kg_05/umap/"
              L"kg_05_enemy_Area_E.kg_05_enemy_Area_E:PersistentLevel"})
        {
            // Story-owned arenas must preserve vanilla actors, including on load.
            assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::vanilla);
        }
        assert(runtime_party_map_policy(
                   L"Level /Game/Levels/ew/ew_02/umap/ew_02.ew_02:PersistentLevel") ==
               RuntimePartyMapPolicy::solo);
        assert(runtime_party_map_policy(
                   L"Level /Game/Levels/kg_DLC/kg_08/kg_08.kg_08:PersistentLevel") ==
               RuntimePartyMapPolicy::solo);
        assert(runtime_party_map_policy(
                   L"Level /Game/Levels/kg_DLC/kg_04/kg_04.kg_04:PersistentLevel") ==
               RuntimePartyMapPolicy::solo);
        assert(runtime_party_map_policy(
                   L"Level /Game/Levels/kg_DLC/kg_04/kg_050.kg_050:PersistentLevel") ==
               RuntimePartyMapPolicy::solo);
        for (auto level :
             {L"Level /Game/Levels/he/he_01/he_01.HE_01:PersistentLevel",
              L"Level /Game/Levels/he/he_02/he_02.HE_02:PersistentLevel",
              L"Level /Game/Levels/he/he_03/he_03.HE_03:PersistentLevel",
              L"Level /Game/Levels/he/he_040/he_040.he_040:PersistentLevel",
              L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel",
              L"Level /Game/Levels/bx/bx_030/bx_030.bx_030:PersistentLevel",
              L"Level /Game/Levels/kg/kg_01/kg_01.kg_01:PersistentLevel",
              L"Level /Game/Levels/kg/kg_03/kg_03.kg_03:PersistentLevel",
              L"Level /Game/Levels/kg/kg_020/kg_020.kg_020:PersistentLevel",
              L"Level /Game/Levels/kg_DLC/kg_02/kg_02.kg_02:PersistentLevel",
              L"Level /Game/Levels/kg_DLC/kg_050/kg_050.kg_050:PersistentLevel",
              L"Level /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01:PersistentLevel"})
        {
            assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
        }
    }

} // namespace

int main()
{
    using namespace kh3ap;
    test_flash_tracer_context();
    test_ship_control_context();
    test_story_map_boundaries();
    struct NativeCompanion
    {
        std::wstring_view world, character;
    };
    constexpr NativeCompanion native_companions[]{
        {L"he", L"hercules"}, {L"ts", L"woody"},       {L"ts", L"buzz"}, {L"ra", L"rapunzel"},
        {L"ra", L"flynn"},    {L"mi", L"sulley"},      {L"mi", L"mike"}, {L"ca", L"jack"},
        {L"bx", L"baymax"},   {L"fz", L"marshmallow"},
    };
    for (const auto& [world, character] : native_companions)
    {
        const auto root = L"/Game/Levels/" + std::wstring{world} + L"/";
        for (auto suffix : {L"01", L"02", L"04"})
        {
            const auto map = std::wstring{world} + L"_" + suffix;
            const auto level =
                L"Level " + root + map + L"/" + map + L"." + map + L":PersistentLevel";
            assert(runtime_party_is_native_world_companion(level, character));
            assert(
                runtime_party_is_native_world_companion(root + map + L"/umap/gameflow", character));
            // Only natives of this world are removed from the random pool.
            for (const auto& other : native_companions)
            {
                assert(runtime_party_is_native_world_companion(level, other.character) ==
                       (world == other.world));
            }
            for (auto non_native : {L"donald", L"goofy", L"mickey", L"riku", L"aqua", L"ventus",
                                    L"lea", L"kairi", L"roxas", L"xion", L"none"})
            {
                assert(!runtime_party_is_native_world_companion(level, non_native));
            }
        }
        // Traveling back to GoA or another world makes the member eligible again.
        for (auto level :
             {L"Level /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01:PersistentLevel",
              L"/Game/Levels/kg/kg_01/", L""})
        {
            assert(!runtime_party_is_native_world_companion(level, character));
        }
        assert(!runtime_party_is_native_world_companion(
            L"/Game/Levels/" + std::wstring{world} + L"_DLC/", character));
    }
    for (auto level :
         {L"Level /Game/Levels/bx/bx_03/bx_03.bx_03:PersistentLevel",
          L"Level /Game/Levels/bx/bx_03/umap/bx_03_gameflow.bx_03_gameflow:PersistentLevel"})
    {
        assert(runtime_party_is_character_disabled(level, L"marshmallow"));
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::vanilla);
        for (auto other : {L"donald", L"goofy", L"baymax", L"xion", L"none", L"vanilla"})
        {
            assert(!runtime_party_is_character_disabled(level, other));
        }
    }
    for (auto level :
         {L"/Game/Levels/bx/bx_01/", L"/Game/Levels/bx/bx_02/", L"/Game/Levels/bx/bx_030/",
          L"/Game/Levels/fz/fz_01/", L"/Game/Maps/GardenOfAssemblage/umap/GoA_01/", L""})
    {
        assert(!runtime_party_is_character_disabled(level, L"marshmallow"));
    }
    for (auto level :
         {L"Level /Game/Levels/fz/fz_03/fz_03.fz_03:PersistentLevel",
          L"Level /Game/Levels/fz/fz_03/fz_03.FZ_03:PersistentLevel",
          L"Level /Game/Levels/fz/fz_03/umap/fz_03_gameflow.fz_03_gameflow:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::vanilla);
        assert(!runtime_party_requires_context_reconcile(level));
    }
    for (auto level : {L"/Game/Levels/fz/fz_030/", L"/Game/Levels/fz/fz_01/",
                       L"/Game/Levels/fz/fz_02/", L"/Game/Levels/fz/fz_04/"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
    }
    for (auto level :
         {L"Level /Game/Levels/bt/bt_03/bt_03.bt_03:PersistentLevel",
          L"Level /Game/Levels/bt/bt_03/umap/bt_03_gameflow.bt_03_gameflow:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::vanilla);
        assert(!runtime_party_requires_context_reconcile(level));
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", level).mesh_name.empty());
    }
    for (auto level :
         {L"/Game/Levels/bt/bt_030/", L"/Game/Levels/bt/bt_02/", L"/Game/Levels/bt/bt_04/"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
    }
    for (auto level :
         {L"Level /Game/Levels/kg_DLC/kg_04/kg_04.kg_04:PersistentLevel",
          L"Level /Game/Levels/ss_DLC/ss_01/ss_01.ss_01:PersistentLevel",
          L"Level /Game/Levels/kg_DLC/kg_08/kg_08.kg_08:PersistentLevel",
          L"Level /Game/Levels/kg_DLC/kg_08/umap/kg_08_gameflow.kg_08_gameflow:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::solo);
        assert(!runtime_party_requires_context_reconcile(level));
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", level).mesh_name.empty());
    }
    for (auto level : {L"/Game/Levels/kg_DLC/kg_040/", L"/Game/Levels/ss_DLC/ss_010/",
                       L"/Game/Levels/kg_DLC/kg_080/"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
    }
    assert(runtime_party_map_policy(L"/Game/Levels/kg_DLC/kg_04/kg_05.kg_05:PersistentLevel") ==
           RuntimePartyMapPolicy::vanilla);
    for (auto level :
         {L"Level /Game/Levels/ts/ts_03/ts_03.ts_03:PersistentLevel",
          L"Level /Game/Levels/ts/ts_03/umap/ts_03_gameflow.ts_03_gameflow:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::solo);
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", level).mesh_name.empty());
        assert(!runtime_party_requires_context_reconcile(level));
    }
    for (auto level :
         {L"/Game/Levels/ts/ts_030/", L"/Game/Levels/ts/ts_02/", L"/Game/Levels/ts/ts_04/"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
    }
    for (auto level :
         {L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel",
          L"Level /Game/Levels/bx/bx_02/umap/bx_02_gameflow.bx_02_gameflow:PersistentLevel"})
    {
        for (int progress : {710, 800, 810, 811, 812, 820, 899})
        {
            assert(runtime_party_map_policy(level, -1, progress) == RuntimePartyMapPolicy::vanilla);
        }
        for (int progress : {-1, 0, 640, 650, 660, 670, 700, 709, 900, 9999, 10000})
        {
            assert(runtime_party_map_policy(level, -1, progress) == RuntimePartyMapPolicy::managed);
        }
    }
    assert(runtime_party_map_policy(L"/Game/Levels/bx/bx_020/", -1, 800) ==
           RuntimePartyMapPolicy::managed);
    assert(runtime_party_map_policy(L"/Game/Levels/bx/bx_01/", -1, 800) ==
           RuntimePartyMapPolicy::managed);
    for (auto level :
         {L"Level /Game/Levels/ex/ex_38/ex_38.ex_38:PersistentLevel",
          L"Level /Game/Levels/ex/ex_38/umap/ex_38_gameflow.ex_38_gameflow:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::solo);
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", level).mesh_name.empty());
        assert(!runtime_party_requires_context_reconcile(level));
    }
    assert(runtime_party_map_policy(L"/Game/Levels/ex/ex_380/") == RuntimePartyMapPolicy::managed);
    assert(runtime_party_map_policy(L"/Game/Levels/ex/ex_37/") == RuntimePartyMapPolicy::managed);
    assert(runtime_party_map_policy(L"/Game/Levels/dw/dw_21/dw_21.DW_21:PersistentLevel") ==
           RuntimePartyMapPolicy::solo);
    assert(runtime_party_map_policy(
               L"/Game/Levels/dw/dw_21/umap/dw_21_gameflow.dw_21_gameflow:PersistentLevel") ==
           RuntimePartyMapPolicy::solo);
    assert(runtime_party_map_policy(L"/Game/Levels/dw/dw_210/dw_210:PersistentLevel") ==
           RuntimePartyMapPolicy::managed);
    assert(runtime_party_requires_context_reconcile(
        L"Level /Game/Levels/ts/ts_04/ts_04.TS_04:PersistentLevel"));
    assert(runtime_party_requires_context_reconcile(
        L"Level /Game/Levels/ts/ts_04/ts_04.ts_04:PersistentLevel"));
    assert(!runtime_party_requires_context_reconcile(
        L"Level /Game/Levels/ts/ts_04/umap/ts_04_gameflow.ts_04_gameflow:PersistentLevel"));
    assert(!runtime_party_requires_context_reconcile(
        L"Level /Game/Levels/bt_DLC/bt_07/bt_07.BT_07:PersistentLevel"));
    assert(!runtime_party_requires_context_reconcile(
        L"Level /Game/Levels/bt_DLC/bt_07/umap/bt_07_gameflow.bt_07_gameflow:PersistentLevel"));
    assert(!runtime_party_requires_context_reconcile(
        L"Level /Game/Levels/bt_DLC/bt_070/bt_070.bt_070:PersistentLevel"));
    for (int arena = 10; arena <= 22; ++arena)
    {
        const auto root = L"/Game/Levels/rg_DLC/rg_" + std::to_wstring(arena) + L"/";
        assert(runtime_party_map_policy(root) == RuntimePartyMapPolicy::solo);
        assert(runtime_party_map_policy(root + L"umap/gameflow") == RuntimePartyMapPolicy::solo);
        assert(!runtime_party_requires_context_reconcile(root));
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", root).mesh_name.empty());
    }
    for (auto root : {L"/Game/Levels/rg_DLC/rg_01/", L"/Game/Levels/rg_DLC/rg_09/",
                      L"/Game/Levels/rg_DLC/rg_23/", L"/Game/Levels/rg_DLC/rg_150/"})
    {
        assert(runtime_party_map_policy(root) == RuntimePartyMapPolicy::managed);
    }
    for (const auto& map : runtime_party_navigation_maps)
    {
        const auto fallback = runtime_party_navigation_fallback(L"RecastNavMesh-Group2", map.level);
        assert(fallback.mesh_name ==
               (map.group == 3 ? L"RecastNavMesh-Group3" : L"RecastNavMesh-Group1"));
        assert(fallback.radius == (map.group == 3 ? 114.0f : 71.0f));
        assert(fallback.height == (map.group == 3 ? 280.0f : 180.0f));
        std::wstring upper_level{map.level};
        for (auto& c : upper_level)
        {
            if (c >= L'a' && c <= L'z')
            {
                c -= L'a' - L'A';
            }
        }
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", upper_level).mesh_name ==
               fallback.mesh_name);
        assert(
            runtime_party_navigation_fallback(L"RecastNavMesh-Group2", upper_level + L"_Streaming")
                .mesh_name.empty());
        // Already-selected fallback meshes must be stable (no oscillation).
        assert(runtime_party_navigation_fallback(fallback.mesh_name, map.level).mesh_name.empty());
        for (auto nav : {L"RecastNavMesh-Group1", L"RecastNavMesh-Group20"})
        {
            assert(runtime_party_navigation_fallback(nav, map.level).mesh_name.empty());
        }
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2",
                                                 std::wstring(map.level) + L"_Streaming")
                   .mesh_name.empty());
    }
    for (const auto& map : runtime_party_large_navigation_maps)
    {
        const auto fallback = runtime_party_navigation_fallback(L"RecastNavMesh-Group3", map.level);
        assert(fallback.mesh_name ==
               (map.group == 2 ? L"RecastNavMesh-Group2" : L"RecastNavMesh-Group1"));
        assert(fallback.radius == 71.0f);
        assert(fallback.height == (map.group == 2 ? 270.0f : 180.0f));
        // Restored Thebes still needs its navigation fallback, while Cherry
        // Flan's startup keeps the story-owned Donald/Goofy party.
        assert(runtime_party_map_policy(map.level) ==
               (map.level.find(L"/Game/Levels/he/he_04/") != std::wstring_view::npos
                    ? RuntimePartyMapPolicy::vanilla
                    : RuntimePartyMapPolicy::managed));
        assert(runtime_party_navigation_fallback(fallback.mesh_name, map.level).mesh_name.empty());
        std::wstring upper_level{map.level};
        for (auto& c : upper_level)
        {
            if (c >= L'a' && c <= L'z')
            {
                c -= L'a' - L'A';
            }
        }
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group3", upper_level).mesh_name ==
               fallback.mesh_name);
        for (auto level :
             {std::wstring{map.level} + L"_Streaming", std::wstring{map.level} + L".Actor"})
        {
            assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group3", level)
                       .mesh_name.empty());
        }
        for (auto nav :
             {L"RecastNavMesh-Group1", L"RecastNavMesh-Group4", L"RecastNavMesh-Group30", L""})
        {
            assert(runtime_party_navigation_fallback(nav, map.level).mesh_name.empty());
        }
    }
    for (auto map : {L"he_01", L"he_02", L"he_03", L"he_04", L"he_05", L"he_06"})
    {
        const auto level = L"Level /Game/Levels/he/" + std::wstring{map} + L"/" + map + L"." + map +
                           L":PersistentLevel";
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group3", level).mesh_name ==
               L"RecastNavMesh-Group2");
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", level).mesh_name.empty());
    }
    for (auto level :
         {L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel",
          L"Level /Game/Levels/mi/mi_01/mi_01.mi_01:PersistentLevel",
          L"Level /Game/Levels/ca/ca_04/ca_04.ca_04:PersistentLevel",
          L"Level /Game/Levels/bx/bx_03/bx_03.bx_03:PersistentLevel",
          L"Level /Game/Levels/ex/ex_38/ex_38.ex_38:PersistentLevel",
          L"Level /Game/Levels/rg_DLC/rg_10/rg_10.rg_10:PersistentLevel",
          L"Level /Game/Levels/he/he_030/he_030.he_030:PersistentLevel",
          L"Level /Game/Levels/he/he_03/umap/he_03_gameflow.he_03_gameflow:PersistentLevel"})
    {
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group3", level).mesh_name.empty());
    }
    constexpr auto city = L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel";
    constexpr auto frozen = L"Level /Game/Levels/fz/fz_01/fz_01.fz_01:PersistentLevel";
    const auto bx_nav = runtime_party_navigation_fallback(L"RecastNavMesh-Group2", city);
    assert(bx_nav.mesh_name == L"RecastNavMesh-Group3");
    assert(bx_nav.radius == 114.0f && bx_nav.height == 280.0f);
    const auto fz_nav = runtime_party_navigation_fallback(L"RecastNavMesh-Group2", frozen);
    assert(fz_nav.mesh_name == L"RecastNavMesh-Group1");
    assert(fz_nav.radius == 71.0f && fz_nav.height == 180.0f);
    constexpr auto marshmallow = L"n_fz214_Pawn_C";
    constexpr auto corona = L"Level /Game/Levels/ra/ra_01/ra_01.ra_01:PersistentLevel";
    constexpr auto group4 = L"RecastNavMesh-Group4";
    for (auto current : {L"", group4, L"RecastNavMesh-Group2"})
    {
        const auto profile = runtime_party_navigation_fallback(current, corona, marshmallow);
        assert(profile.mesh_name == L"RecastNavMesh-Group2");
        assert(profile.radius == 71.0f && profile.height == 270.0f);
    }
    const auto marshmallow_city = runtime_party_navigation_fallback(group4, city, marshmallow);
    assert(marshmallow_city.mesh_name == bx_nav.mesh_name);
    assert(marshmallow_city.radius == bx_nav.radius && marshmallow_city.height == bx_nav.height);
    assert(runtime_party_navigation_fallback(marshmallow_city.mesh_name, city, marshmallow)
               .mesh_name == marshmallow_city.mesh_name);
    assert(runtime_party_navigation_fallback(group4, frozen, marshmallow).mesh_name.empty());
    assert(runtime_party_navigation_fallback(
               group4, L"Level /GAME/LEVELS/FZ/fz_04/fz_04.FZ_04:PersistentLevel", marshmallow)
               .mesh_name.empty());
    assert(runtime_party_navigation_fallback(group4, corona, L"n_mi201_Pawn_C").mesh_name.empty());
    constexpr auto frozen04 = L"Level /Game/Levels/fz/fz_04/fz_04.FZ_04:PersistentLevel";
    const auto fz04_nav = runtime_party_navigation_fallback(L"RecastNavMesh-Group2", frozen04);
    assert(fz04_nav.mesh_name == L"RecastNavMesh-Group1");
    assert(fz04_nav.radius == 71.0f && fz04_nav.height == 180.0f);
    constexpr auto scala = L"Level /Game/Levels/bt/bt_01/bt_01.bt_01:PersistentLevel";
    const auto scala_nav = runtime_party_navigation_fallback(L"RecastNavMesh-Group2", scala);
    assert(scala_nav.mesh_name == L"RecastNavMesh-Group1");
    assert(scala_nav.radius == 71.0f && scala_nav.height == 180.0f);
    const auto marshmallow_scala = runtime_party_navigation_fallback(group4, scala, marshmallow);
    assert(marshmallow_scala.mesh_name == scala_nav.mesh_name);
    assert(marshmallow_scala.radius == scala_nav.radius &&
           marshmallow_scala.height == scala_nav.height);
    constexpr auto scala04 = L"Level /Game/Levels/bt/bt_04/bt_04.bt_04:PersistentLevel";
    const auto scala04_nav = runtime_party_navigation_fallback(L"RecastNavMesh-Group2", scala04);
    assert(scala04_nav.mesh_name == L"RecastNavMesh-Group1");
    assert(scala04_nav.radius == 71.0f && scala04_nav.height == 180.0f);
    for (auto level : {city, frozen, frozen04, scala, scala04})
    {
        for (auto nav :
             {L"RecastNavMesh-Group1", L"RecastNavMesh-Group4", L"RecastNavMesh-Group20", L""})
        {
            assert(runtime_party_navigation_fallback(nav, level).mesh_name.empty());
        }
    }
    assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group3", scala).mesh_name ==
           L"RecastNavMesh-Group1");
    for (auto level : {city, frozen, frozen04, scala04})
    {
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group3", level).mesh_name.empty());
    }
    for (auto level :
         {L"Level /Game/Levels/bx/bx_03/bx_03.bx_03:PersistentLevel",
          L"Level /Game/Levels/bx/bx_02/umap/bx_02_gameflow.bx_02_gameflow:PersistentLevel",
          L"Level /Game/Levels/bt/bt_010/bt_010.bt_010:PersistentLevel",
          L"Level /Game/Levels/bt/bt_040/bt_040.bt_040:PersistentLevel",
          L"Level /Game/Levels/bt/bt_04/umap/bt_04_gameflow.bt_04_gameflow:PersistentLevel",
          L"Level /Game/Levels/bt/bt_01/umap/bt_01_gameflow.bt_01_gameflow:PersistentLevel",
          L"Level /Game/Levels/fz/fz_02/fz_02.fz_02:PersistentLevel",
          L"Level /Game/Levels/fz/fz_040/fz_040.FZ_040:PersistentLevel",
          L"Level /Game/Levels/fz/fz_04/umap/fz_04_gameflow.fz_04_gameflow:PersistentLevel",
          L"Level /Game/Levels/fz/fz_010/fz_010.fz_010:PersistentLevel",
          L"Level /Game/Levels/fz/fz_01/umap/fz_01_gameflow.fz_01_gameflow:PersistentLevel",
          L"Level /Game/Levels/bx/bx_020/bx_020.bx_020:PersistentLevel"})
    {
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", level).mesh_name.empty());
    }
    for (auto level :
         {L"Level /Game/Levels/bx/bx_03/bx_03.bx_03:PersistentLevel",
          L"Level /Game/Levels/bx/bx_03/umap/bx_03_gameflow.bx_03_gameflow:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::vanilla);
    }
    for (auto level : {L"Level /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel",
                       L"Level /Game/Levels/bx/bx_030/bx_030.bx_030:PersistentLevel",
                       L"Level /Game/Levels/he/he_02/he_02.he_02:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
    }
    for (auto level :
         {L"Level /Game/Levels/kg_DLC/kg_06/kg_06.kg_06:PersistentLevel",
          L"Level /Game/Levels/kg_DLC/kg_06/umap/kg_06_gameflow.kg_06_gameflow:PersistentLevel"})
    {
        // Both friend slots belong to the encounter, including Kairi on Retry.
        for (int progress : {-1, 0, 20100, 21200, 21300, 22100, 22200, 22250, 99999})
        {
            assert(runtime_party_map_policy(level, progress) == RuntimePartyMapPolicy::vanilla);
        }
    }
    for (auto level : {L"Level /Game/Levels/bt/bt_01/bt_01.bt_01:PersistentLevel",
                       L"Level /Game/Levels/kg_DLC/kg_060/kg_060.kg_060:PersistentLevel",
                       L"Level /Game/Levels/bt_DLC_extra/bt_08/bt_08.bt_08:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::managed);
    }
    for (auto level :
         {L"Level /Game/Levels/bt_DLC/bt_07/bt_07.bt_07:PersistentLevel",
          L"Level /Game/Levels/bt_DLC/bt_08/bt_08.bt_08:PersistentLevel",
          L"Level /Game/Levels/bt_DLC/bt_07/umap/bt_07_gameflow.bt_07_gameflow:PersistentLevel",
          L"Level /Game/Levels/bt_DLC/bt_08/umap/bt_08_gameflow.bt_08_gameflow:PersistentLevel"})
    {
        assert(runtime_party_map_policy(level) == RuntimePartyMapPolicy::solo);
        assert(!runtime_party_requires_context_reconcile(level));
        assert(runtime_party_navigation_fallback(L"RecastNavMesh-Group2", level).mesh_name.empty());
    }
    assert(runtime_party_requires_entry_reconcile(
        L"Level /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01:PersistentLevel"));
    assert(!runtime_party_requires_entry_reconcile(
        L"Level /Game/Maps/GardenOfAssemblage/umap/GoA_010.GoA_010:PersistentLevel"));
    assert(!runtime_party_requires_entry_reconcile(
        L"Level "
        L"/Game/Maps/StationOfAwakening/umap/"
        L"StationOfAwakening.StationOfAwakening:PersistentLevel"));
    assert(!runtime_party_requires_entry_reconcile(
        L"Level /Game/Levels/fz/fz_01/fz_01.fz_01:PersistentLevel"));
    assert(runtime_party_map_policy(L"/Game/Levels/ew/ew_01/") == RuntimePartyMapPolicy::solo);
    assert(runtime_party_map_policy(L"/Game/Levels/kg/kg_02/") == RuntimePartyMapPolicy::vanilla);
}
