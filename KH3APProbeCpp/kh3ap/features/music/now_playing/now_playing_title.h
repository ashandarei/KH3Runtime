#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap {
// Asset names are stable across shuffle modes. Resolve the track actually on
// the slot, not the original world/target. Soundtrack spelling reference:
// https://www.jp.square-enix.com/music/sem/page/kh3/
inline auto native_song_title(std::wstring_view path) -> std::wstring {
    const auto separator=path.find_last_of(L"/.");
    std::wstring name{separator==path.npos?path:path.substr(separator+1)};
    std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return std::towlower(c);});
    static constexpr std::pair<std::wstring_view,std::wstring_view> titles[]{
        {L"bgm_he_field",L"Mount Olympus"},
        {L"bgm_he_field_2",L"Olympus Coliseum -The Shining Summit-"},
        {L"bgm_he_battle",L"Hero's Fanfare"},
        {L"bgm_he_battle_2",L"Go for It! -Into the Clouds-"},
        {L"bgm_theafter_field",L"The Afternoon Streets"},
        {L"bgm_workingt",L"Working Together"},
        {L"bgm_ts_field",L"You've Got a Friend in Me"},
        {L"bgm_ts_battle",L"Toy Box Jam"},
        {L"bgm_ra_field",L"Happy Hair Day"},
        {L"bgm_ra_field_2",L"Happy Hair Day -Into the Forest Deep-"},
        {L"bgm_ra_battle",L"Swingin' Free"},
        {L"bgm_ra_battle_2",L"Swingin' Free -Into the Forest Deep-"},
        {L"bgm_mi_field",L"Monstropolis Now"},
        {L"bgm_mi_field_2",L"Monstropolis Now -Code 72-16-"},
        {L"bgm_mi_battle",L"Monster Smash!"},
        {L"bgm_mi_battle_2",L"Monster Smash! -Code 72-16-"},
        {L"bgm_fz_field",L"Frozen Wonderland"},
        {L"bgm_fz_field_2",L"Frozen Wonderland -Eternal Winter-"},
        {L"bgm_fz_battle",L"Miracle on Ice"},
        {L"bgm_fz_battle_2",L"Miracle on Ice -Eternal Winter-"},
        {L"bgm_ca_field",L"A Pirate's Adventure"},
        {L"bgm_ca_field_2",L"A Pirate's Freedom"},
        {L"bgm_ca_field_3",L"Winds of Fate"},
        {L"bgm_ca_battle",L"Flags of Fury"},
        {L"bgm_ca_battle_2",L"Flags of Fury -Leviathan Awakened-"},
        {L"bgm_bx_field",L"AR -Augmented Rhythm-"},
        {L"bgm_bx_field_2",L"AR -Augmented Rhythm-"},
        {L"bgm_bx_battle",L"Robot Overdrive"},
        {L"bgm_poo_field",L"Winnie the Pooh"},
        {L"bgm_poo_mini",L"Bounce-O-Rama"},
        {L"bgm_kg_field",L"Graveyard Labyrinth"},
        {L"bgm_ew_field",L"Scala ad Caelum"},
        {L"bgm_ew_battle",L"Edge of Existence"},
        {L"bgm_ew751_cut",L"Stranded Beyond"},
        {L"bgm_field_rg03",L"Cavern of Remembrance"},
        {L"bgm_attraction",L"Hand in Hand -Attraction Flow Version-"},
        {L"bgm_link_dream",L"Dream Eaters -Link Version-"},
        {L"bgm_link_ralph",L"8Bit Smasher -Link Version-"},
        {L"bgm_link_simba",L"Savannah Pride -Link Version-"},
        {L"bgm_link_ariel",L"An Adventure in Atlantica -Link Version-"},
        {L"bgm_link_stitch",L"M\u00e1kaukau? -Link Version-"},
        {L"bgm_flashtracer",L"Flash Tracer"},
        {L"bgm_mountcurling",L"The Coolest Contest"},
        {L"bgm_lsi_menu",L"Pixel Hero"},
        {L"bgm_cook_menu",L"The Little Chef"},
        {L"bgm_pudding_battle",L"Flantastic Fight"},
        {L"bgm_pudding_camera",L"Say Cheese!"},
        {L"bgm_pudding_he",L"Go! Go! Rumble Racer -Pudding Grand Prix-"},
        {L"bgm_world_field",L"Sky of Wonder"},
        {L"bgm_gumi_edit",L"Shipmeisters' Shanty"},
        {L"bgm_gumi_field_1",L"Star Explorer"},
        {L"bgm_gumi_field_2",L"Star Explorer"},
        {L"bgm_gumi_field_3",L"Star Explorer"},
        {L"bgm_gumi_battle",L"Lock, Load, and Blast!"},
        {L"bgm_rocktitan_battle",L"The Deep End -Rock Titan's Rage-"},
        {L"bgm_shrouding_battle",L"Shrouding Dark Cloud -Gigas Blast-"},
        {L"bgm_xeha_bat__boss_1",L"Replicas"},
        {L"bgm_xeha_bat__boss_2",L"Critical Crossroad"},
        {L"bgm_xeha_bat__boss_3",L"Dark Domination"},
        {L"bgm_battle_yozora",L"Nachtfl\u00fcgel"},
        {L"bgm_kg852b_battle",L"Rise of the Union"},
    };
    for(const auto& [key,title]:titles) if(name==key) return std::wstring{title};
    // Unlisted cutscene cues remain identifiable instead of claiming an
    // unrelated soundtrack title. Preserve their original case and numbers.
    name=separator==path.npos?path:path.substr(separator+1);
    if(name.starts_with(L"BGM_") || name.starts_with(L"bgm_")) name.erase(0,4);
    std::replace(name.begin(),name.end(),L'_',L' ');
    return name;
}
}
