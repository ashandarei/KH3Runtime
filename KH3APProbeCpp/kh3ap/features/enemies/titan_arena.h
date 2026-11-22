#pragma once
#include <string_view>
namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    void configure_titan_arena_replacement(std::wstring_view source, std::wstring_view target);
    void track_titan_arena_entity(RC::Unreal::UObject* entity,
            std::wstring_view source, std::wstring_view target);
    void process_titan_arena_replacement(bool enabled);
    void notify_titan_arena_binding(std::wstring_view entity_path);
    void install_replacement_titan_attack_support();
    [[nodiscard]] bool install_titan_hit_actor_guard();
    [[nodiscard]] bool install_titan_partner_guard();
    // Exact cooked actors; the enclosing battle wall and floor are excluded.
    inline constexpr std::wstring_view titan_arena_obstacles[]{
        L"/Game/Levels/he/he_05/umap/he_05_gimmick_01.he_05_gimmick_01:PersistentLevel.he_05_invisiblewall_01",
        L"/Game/Levels/he/he_05/umap/he_05_gimmick_01.he_05_gimmick_01:PersistentLevel.he_05_invisiblewall_02",
        L"/Game/Maps/he/umap/he_05/he_05_map_areaA.he_05_map_areaA:PersistentLevel.SM_he_Ter_ZeusRockBattleA",
    };
}
