#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kh3ap
{
    inline constexpr float boss_encounter_min_progress_seconds = 20.0f;
    inline constexpr auto boss_encounter_damaged_hp_percent = 90;

    struct KgBossRevealEvent
    {
        std::wstring_view name;
        wchar_t area;
    };
    // These cooked B/C/D arena branches hide all bosses for cinematics, then
    // put their reveal behind a TresEnemyXIIIPawnBase cast. Non-XIII replacements
    // miss that reveal. Combat actions and ordinary visibility calls are excluded.
    inline constexpr std::array<KgBossRevealEvent, 15> kg_boss_reveal_events{{
        {L"cutscene_kg02_kg861_REvt_show_EnemyPawn", L'B'},
        {L"kg_02_gameflow_REvt_BattleArea_B_SetPhase1", L'B'},
        {L"kg_02_gameflow_REvt_BattleArea_B_SetPhase2", L'B'},
        {L"kg_02_gameflow_REvt_BattleArea_B_SetPhase3", L'B'},
        {L"kg_02_gameflow_REvt_BattleArea_B_SetPhase4", L'B'},
        {L"cutscene_kg02_kg867_REvt_show_EnemyPawn", L'C'},
        {L"cutscene_kg05_kg957_REvt_show_EnemyPawn", L'C'},
        {L"kg_02_gameflow_REvt_BattleArea_C_SetPhase1", L'C'},
        {L"kg_02_gameflow_REvt_BattleArea_C_SetPhase2", L'C'},
        {L"kg_02_gameflow_REvt_BattleArea_C_SetPhaseA", L'C'},
        {L"cutscene_kg05_kg872s_REvt_show_EnemyPawn", L'D'},
        {L"cutscene_kg02_kg872_REvt_BattleArea_D_SetPhase2", L'D'},
        {L"kg_02_gameflow_REvt_BattleArea_D_SetPhase1", L'D'},
        {L"kg_02_gameflow_REvt_BattleArea_D_SetPhase2", L'D'},
        {L"kg_05_gameflow_REvt_BattleArea_D_SetPhaseA", L'D'},
    }};

    [[nodiscard]] inline auto is_kg_boss_reveal_level(std::wstring_view level, wchar_t area) -> bool
    {
        if (area < L'B' || area > L'D')
        {
            return false;
        }
        for (const auto prefix : {L"/Game/Levels/kg/kg_02/umap/kg_02_enemy_Area_",
                                  L"/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_"})
        {
            const std::wstring package = std::wstring{prefix} + area;
            const auto name = package.substr(package.rfind(L'/') + 1);
            if (level == package + L"." + name + L":PersistentLevel")
            {
                return true;
            }
        }
        return false;
    }

    inline constexpr std::wstring_view remind_xigbar_class =
        L"/Game/Blueprints/Enemy/e_ex305_s10/e_ex305_Pawn_s10.e_ex305_Pawn_s10_C";
    [[nodiscard]] inline auto needs_remind_ansem_phase_driver(std::wstring_view source,
                                                              std::wstring_view replacement) -> bool
    {
        return source == remind_xigbar_class && !replacement.empty() &&
               replacement != remind_xigbar_class &&
               replacement != L"/Game/Blueprints/Enemy/e_ex305/e_ex305_Pawn.e_ex305_Pawn_C";
    }

    // Xigbar's native phase-I driver: the cooked pawn overrides battle time
    // to 20 seconds and inherits its 90-percent HP threshold. Opening Ansem
    // only retreats; this does not complete either remaining boss slot.
    [[nodiscard]] inline auto remind_ansem_retreat_due(float battle_seconds, int lowest_hp_percent)
        -> bool
    {
        return battle_seconds >= boss_encounter_min_progress_seconds ||
               (lowest_hp_percent >= 0 && lowest_hp_percent <= boss_encounter_damaged_hp_percent);
    }

    [[nodiscard]] inline auto needs_larxene_solo_phase(std::wstring_view source,
                                                       std::wstring_view replacement) -> bool
    {
        return !source.empty() && source != replacement &&
               (replacement == L"/Game/Blueprints/Enemy/e_ex309/e_ex309_Pawn.e_ex309_Pawn_C" ||
                replacement ==
                    L"/Game/Blueprints/Enemy/e_ex309_s10/e_ex309_Pawn_s10.e_ex309_Pawn_s10_C");
    }

    // Tower mode requires Corona's tower/warp actors; randomized pawns use
    // their native normal-mode attacks instead.
    inline constexpr std::wstring_view grim_guardianess_class =
        L"/Game/Blueprints/Enemy/e_ex721/e_ex721_Pawn.e_ex721_Pawn_C";

    [[nodiscard]] inline auto needs_grim_guardianess_normal_mode(std::wstring_view source,
                                                                 std::wstring_view replacement)
        -> bool
    {
        return !source.empty() && source != grim_guardianess_class &&
               replacement == grim_guardianess_class;
    }

    [[nodiscard]] inline auto is_grim_guardianess_tower_rising_task(std::wstring_view tree,
                                                                    std::wstring_view locomotion)
        -> bool
    {
        return tree == L"/Game/Blueprints/Enemy/e_ex721/BT/e_ex721_BT.e_ex721_BT" &&
               locomotion ==
                   L"/Game/Blueprints/Enemy/e_ex721/BT/e_ex721_Move4_Rising.e_ex721_Move4_Rising_C";
    }

    [[nodiscard]] inline auto is_grim_guardianess_flash_action(std::wstring_view tree,
                                                               std::wstring_view action) -> bool
    {
        return tree == L"/Game/Blueprints/Enemy/e_ex721/BT/e_ex721_BT.e_ex721_BT" &&
               action == L"/Game/Blueprints/Enemy/e_ex721/BT/"
                         L"e_ex721_Attack7_Flash_T.e_ex721_Attack7_Flash_T_C";
    }

    // Arena-specific exclusions from live transition audits and operator review.
    inline constexpr std::array<int, 13> data_boss_ids{351, 352, 353, 354, 355, 356, 357,
                                                       358, 359, 360, 361, 362, 367};

    inline auto remove_incompatible_boss_arena_replacements(
        std::unordered_map<std::wstring, std::wstring>& replacements) -> size_t
    {
        return std::erase_if(
            replacements,
            [](const auto& entry)
            {
                // Protect existing seeds from all Young Xehanort variants in the
                // Ice/Lava Titan encounter, retaining the native Titan instead.
                if ((entry.first == L"/Game/Blueprints/Enemy/e_he902/e_he902_Pawn.e_he902_Pawn_C" ||
                     entry.first ==
                         L"/Game/Blueprints/Enemy/e_he904/e_he904_Pawn.e_he904_Pawn_C") &&
                    (entry.second ==
                         L"/Game/Blueprints/Enemy/e_ex302/e_ex302_Pawn.e_ex302_Pawn_C" ||
                     entry.second == L"/Game/Blueprints/Enemy/e_ex302_s10/"
                                     L"e_ex302_Pawn_s10.e_ex302_Pawn_s10_C" ||
                     entry.second == L"/Game/Blueprints/Enemy/e_ex351/e_ex351_Pawn.e_ex351_Pawn_C"))
                {
                    return true;
                }

                if (entry.first == L"/Game/Blueprints/Enemy/e_ex701/e_ex701_Pawn.e_ex701_Pawn_C" &&
                    (entry.second ==
                         L"/Game/Blueprints/Enemy/e_ex721/e_ex721_Pawn.e_ex721_Pawn_C" ||
                     // Monsters, Inc. audit: sustained Data Ansem displacement;
                     // Data Marluxia excluded after operator-reported Doom-tick vanish.
                     entry.second ==
                         L"/Game/Blueprints/Enemy/e_ex352/e_ex352_Pawn.e_ex352_Pawn_C" ||
                     entry.second == L"/Game/Blueprints/Enemy/e_ex357/e_ex357_Pawn.e_ex357_Pawn_C"))
                {
                    return true;
                }
                return entry.first ==
                           L"/Game/Blueprints/Enemy/e_fz903/e_fz903_Pawn.e_fz903_Pawn_C" &&
                       (entry.second ==
                            L"/Game/Blueprints/Enemy/e_ex721/e_ex721_Pawn.e_ex721_Pawn_C" ||
                        entry.second ==
                            L"/Game/Blueprints/Enemy/e_ex711/e_ex711_Pawn.e_ex711_Pawn_C" ||
                        // All Data encounters are excluded from Marshmallow's arena.
                        std::any_of(std::begin(data_boss_ids), std::end(data_boss_ids),
                                    [&](int id)
                                    {
                                        const auto pawn = L"e_ex" + std::to_wstring(id);
                                        return entry.second == L"/Game/Blueprints/Enemy/" + pawn +
                                                                   L"/" + pawn + L"_Pawn." + pawn +
                                                                   L"_Pawn_C";
                                    }));
            });
    }

    [[nodiscard]] inline auto is_story_terranort_class(std::wstring_view path) -> bool
    {
        return path == L"/Game/Blueprints/Enemy/e_ex325/e_ex325_Pawn.e_ex325_Pawn_C" ||
               path == L"/Game/Blueprints/Enemy/e_ex325_s0/e_ex325_Pawn_s0.e_ex325_Pawn_s0_C" ||
               path == L"/Game/Blueprints/Enemy/e_ex325_s10/e_ex325_Pawn_s10.e_ex325_Pawn_s10_C";
    }

    inline constexpr std::wstring_view final_xehanort_class =
        L"/Game/Blueprints/Enemy/e_ex301/e_ex301_Pawn.e_ex301_Pawn_C";
    inline constexpr std::wstring_view scala_first_xehanort_class =
        L"/Game/Blueprints/Enemy/e_ex301_s0/e_ex301_Pawn_s0.e_ex301_Pawn_s0_C";
    inline constexpr std::wstring_view scala_transition_event =
        L"program_e_ex301_REvt_Summon_KingdomHearts";

    inline constexpr std::wstring_view kg_demon_tide_class =
        L"/Game/Blueprints/Enemy/e_dw401_s1/e_dw401_Pawn_s1.e_dw401_Pawn_s1_C";
    inline constexpr std::wstring_view kg_demon_tide_transition_event =
        L"program_e_dw401_s1_REvt_Unioncross_Start";

    [[nodiscard]] inline auto is_kg_demon_tide_transition(std::wstring_view entity,
                                                          std::wstring_view source,
                                                          std::wstring_view group) -> bool
    {
        return source == kg_demon_tide_class &&
               entity == L"/Game/Levels/kg/kg_01/umap/"
                         L"kg_01_enemy_02.kg_01_enemy_02:PersistentLevel.e_dw401_Pawn_s1" &&
               group == L"kg_01_02_en_Group_01";
    }

    [[nodiscard]] inline auto is_scala_final_transition(std::wstring_view entity,
                                                        std::wstring_view source,
                                                        std::wstring_view group) -> bool
    {
        return source == scala_first_xehanort_class &&
               entity == L"/Game/Levels/bt/bt_04/umap/"
                         L"bt_04_enemy_02.bt_04_enemy_02:PersistentLevel.e_ex301_Pawn_s0_1" &&
               group == L"bt_04_en_02_Group_01";
    }

    inline auto
    remove_final_xehanort_replacements(std::unordered_map<std::wstring, std::wstring>& replacements)
        -> size_t
    {
        return std::erase_if(replacements,
                             [](const auto& entry)
                             {
                                 return entry.first == final_xehanort_class ||
                                        entry.second == final_xehanort_class;
                             });
    }

    inline constexpr std::array<std::wstring_view, 7> lich_boss_classes{
        L"/Game/Blueprints/Enemy/e_ex047/e_ex047_Pawn.e_ex047_Pawn_C",
        L"/Game/Blueprints/Enemy/e_ex047_s0/e_ex047_Pawn_s0.e_ex047_Pawn_s0_C",
        L"/Game/Blueprints/Enemy/e_ex047_s2/e_ex047_Pawn_s2.e_ex047_Pawn_s2_C",
        L"/Game/Blueprints/Enemy/e_ex047_s1/e_ex047_Pawn_s1.e_ex047_Pawn_s1_C",
        L"/Game/Blueprints/Enemy/e_ex047_s3/e_ex047_Pawn_s3.e_ex047_Pawn_s3_C",
        L"/Game/Blueprints/Enemy/e_ca047/e_ca047_Pawn.e_ca047_Pawn_C",
        L"/Game/Blueprints/Enemy/e_ex047_s4/e_ex047_Pawn_s4.e_ex047_Pawn_s4_C",
    };

    [[nodiscard]] inline auto is_lich_boss_class(std::wstring_view path) -> bool
    {
        return std::find(lich_boss_classes.begin(), lich_boss_classes.end(), path) !=
               lich_boss_classes.end();
    }

    // Old seeds can still contain Lich swaps. Dropping both incoming and
    // outgoing swaps lets each affected entity retain its native boss class.
    inline auto
    remove_lich_boss_replacements(std::unordered_map<std::wstring, std::wstring>& replacements)
        -> size_t
    {
        return std::erase_if(replacements,
                             [](const auto& entry)
                             {
                                 return is_lich_boss_class(entry.first) ||
                                        is_lich_boss_class(entry.second);
                             });
    }
} // namespace kh3ap
