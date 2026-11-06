#pragma once
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace kh3ap::checks
{
    inline constexpr int classic_kingdom_game_count = 23;

    struct Location { std::string key, table, id; };
    class Policy
    {
    public:
        std::map<std::string, Location> locations;
        std::set<std::string> observed, completed;
        bool goal{};

        auto observe(std::string_view table, std::string_view id) -> void
        { observed.insert(std::string(table) + ":" + std::string(id)); }
        auto resolve() -> std::set<std::string>
        {
            auto earned = observed;
            // The seed can expose the Keyblade flag before the Titans clear.
            if (!earned.contains("VBonus:Vbonus_013")) earned.erase("Event:EVENT_KEYBLADE_001");
            if (earned.contains("VBonus:Vbonus_037") && !earned.contains("VBonus:Vbonus_038") &&
                !earned.contains("Event:EVENT_HEARTBINDER_003"))
            {
                earned.insert("VBonus:Vbonus_038");
                earned.insert("Event:EVENT_HEARTBINDER_003");
            }
            bool all_games = true;
            for (int i = 1; i <= classic_kingdom_game_count; ++i)
            {
                all_games &= earned.contains("ClassicKingdomHighScore:LSIGAME" +
                                             std::string(i < 10 ? "0" : "") + std::to_string(i));
            }
            if (all_games) earned.insert("Event:TresUIMobilePortalDataAsset");
            bool all_data_battles = true;
            for (int index = 1; index <= 13; ++index)
            {
                all_data_battles &= earned.contains("Event:EVENT_DATAB_" +
                    std::string(index < 10 ? "00" : "0") + std::to_string(index));
            }
            if (all_data_battles)
            {
                earned.insert("CharacterUnlock:Riku");
            }
            for (const auto prefix : {"RA_MINIGAME", "TS_MINIGAME", "FZ_MINIGAME", "BX_MINIGAME_1", "BX_MINIGAME_2"})
            {
                bool higher{};
                for (const char rank : std::string_view{"ABCD"})
                {
                    const auto source = std::string(prefix) + "_Rank_" + rank;
                    if (higher) earned.insert("MinigameReward:" + source);
                    higher |= earned.contains("MinigameReward:" + source) ||
                        (rank == 'A' && earned.contains("MinigameReward:" + source + "_1st"));
                }
            }
            std::set<std::string> added;
            for (const auto& [key, row] : locations)
                if (earned.contains(row.table + ":" + row.id) && completed.insert(key).second) added.insert(key);
            return added;
        }
        static auto encounter(std::string_view hook, std::string_view context) -> std::string
        {
            if (hook != "BP_TresGetVictoryBonus") return {};
            for (const auto& [fragment, id] : std::map<std::string_view, std::string_view>{
                {"/Game/Levels/tt/tt_01/umap/tt_01_enemy_06", "Vbonus_016"},
                {"/Game/Levels/mi/mi_01/umap/mi_01_enemy_01", "Vbonus_032"},
                {"/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_B", "VBonus_DLC_005"},
                {"/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_E", "VBonus_DLC_008"}})
                if (context.find(fragment) != std::string_view::npos) return std::string(id);
            return {};
        }
        static auto ending(std::string_view map) -> bool
        { return map == "/Game/Levels/ss/ss_50" || map.starts_with("/Game/Levels/ss/ss_50/"); }
    };
}
