#include "kh3ap/features/kh02/world/kh02_area_selection.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_text.h"
#include "kh3ap/features/kh02/story/kh02_mirror_arrival.h"
#include "kh02/mirror_host_metadata.h"
#include <cassert>
#include <limits>
#include <map>

int main()
{
    using namespace kh3ap;
    const auto opening = kh02_mirror_arrival(false, L"");
    assert(opening.event == L"dw_03_PersistentLevel_REvt_LevelStart_dw015");
    assert(!opening.common_entry_required);
    assert(!opening.host_entry_fade);
    const auto resumed = kh02_mirror_arrival(true, L"");
    assert(resumed.event == kh02_mirror_common_entry);
    assert(!resumed.common_entry_required);
    assert(resumed.host_entry_fade);
    for (int area : {4, 5, 6})
    {
        const auto suffix = L"_dw_0" + std::to_wstring(area) + L"_Clear";
        for (bool resume : {false, true})
        {
            const auto arrival = kh02_mirror_arrival(resume, L"dw_03_Lv_Clear_dw_0" + std::to_wstring(area));
            assert(arrival.common_entry_required);
            assert(!arrival.host_entry_fade);
            assert(arrival.event == std::wstring(kh02_mirror_common_entry) + suffix);
        }
    }
    const auto challenge = kh02_mirror_arrival(false, L"dw_03_Lv_ChallengeClear");
    assert(challenge.common_entry_required);
    assert(!challenge.host_entry_fade);
    assert(challenge.event == L"dw_03_PersistentLevel_REvt_LevelStart_ChallengeEnd");
    const auto ordinary = kh02_mirror_arrival(false, L"dw_03_Lv_Start_01");
    assert(!ordinary.common_entry_required);
    assert(ordinary.host_entry_fade);
    assert(ordinary.event == kh02_mirror_common_entry);

    assert(kh02_field_host_entry_fade(L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest"));
    assert(kh02_field_host_entry_fade(L"/Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths"));
    for (const auto* world : {
             L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown",
             L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin",
             L"/Game/Maps/APKH02Mirrors/APKH02Stairs.APKH02Stairs",
             L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands",
             L"/Game/Maps/APKH02Mirrors/APKH02Transition.APKH02Transition",
             L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest:PersistentLevel.Actor"})
    {
        assert(!kh02_field_host_entry_fade(world));
    }

    assert(kh02_format_challenge_progress(false, 25, 50).empty());
    assert(kh02_format_challenge_progress(true, 25, 0).empty());
    assert(kh02_format_challenge_progress(true, 0, 50) == L"0 / 50");
    assert(kh02_format_challenge_progress(true, 125, 50) == L"50 / 50");
    assert(kh02_format_challenge_text(L"<b>Aqua</b><br>Do {0}, then {0}.", 7, 25) ==
           L"the current player\nDo 25, then 25.");
    assert(kh02_format_challenge_text(L"Aqua", 6, 1) == L"Aqua");
    assert(kh02_format_challenge_text(L"Aqua", 9, 1) == L"the current player");
    assert(kh02_format_challenge_text(L"Aqua", 10, 1) == L"Aqua");
    assert(kh02_format_challenge_text(L"Text <unfinished", 1, 5) == L"Text <unfinished");
    assert(kh02_format_challenge_text(L"<tag></tag>", 1, 5).empty());

    std::map<std::wstring, int> flags;
    const auto visited = [&flags](const std::wstring& flag)
    {
        const auto found = flags.find(flag);
        return found == flags.end() ? 0 : found->second;
    };
    assert(kh02_area_selection(visited).rows.empty());
    flags[kh02_visited_area_flag(9)] = 1;
    flags[kh02_visited_area_flag(2)] = 1;
    flags[kh02_visited_area_flag(3)] = 2;
    const auto selection = kh02_area_selection(visited);
    assert(selection.rows.size() == 2);
    assert(selection.rows[0].text == L"Castle Town");
    assert(selection.rows[1].text == L"Depths of Darkness");
    assert(selection.selected_area(0) == 2);
    assert(selection.selected_area(1) == 9);
    assert(!selection.selected_area(-1));
    assert(!selection.selected_area(2));
    assert(!selection.selected_area(std::numeric_limits<int>::max()));
    assert(kh02_visited_area_destination(9, visited)->area == 9);
    assert(!kh02_visited_area_destination(3, visited));
    flags[kh02_visited_area_flag(999)] = 1;
    assert(!kh02_visited_area_destination(999, visited));
    // Forest source retry events are checkpoint-driven; their arrival tags
    // must stream the matching arena instead of the first-entry mapset.
    assert(std::wstring_view(kh02_area_resume_tag(8, 320, L"initial")) == L"dw_08_Lv_Retry_01");
    assert(std::wstring_view(kh02_area_resume_tag(8, 322, L"initial")) == L"dw_08_Lv_Retry_03");
    assert(std::wstring_view(kh02_area_resume_tag(8, 340, L"initial")) == L"dw_08_Lv_Retry_02");
    assert(kh02_area_entry_checkpoint(8, 330) == 323);
    assert(std::wstring_view(kh02_area_resume_tag(8, 330, L"initial")) == L"dw_08_Lv_Save_02");
    // Completed sections resume at their authored save points without
    // replaying a boss or changing the chapter bank.
    assert(std::wstring_view(kh02_area_resume_tag(8, 300, L"initial")) == L"dw_08_Lv_Save_01");
    assert(std::wstring_view(kh02_area_resume_tag(8, 323, L"initial")) == L"dw_08_Lv_Save_02");
    for (int checkpoint : {350, 360, 410, 520, 9999})
    {
        assert(std::wstring_view(kh02_area_resume_tag(8, checkpoint, L"initial")) == L"dw_08_Lv_Save_03");
    }
    assert(std::wstring_view(kh02_area_resume_tag(8, 280, L"initial")) == L"initial");
    for (int area : {2, 3, 9})
    {
        assert(std::wstring_view(kh02_area_resume_tag(area, 320, L"initial")) == L"initial");
    }
    assert(std::wstring_view(kh02_area_resume_tag(9, 400, L"initial")) == L"initial");
    assert(std::wstring_view(kh02_area_resume_tag(9, 410, L"initial")) == L"dw_09_Lv_Save_01");
    assert(std::wstring_view(kh02_area_resume_tag(9, 9999, L"initial")) == L"dw_09_Lv_Save_01");
    // Travel chooses its spawn before the source save-point actors finish
    // streaming. Every authored resume tag must also exist in the host map.
    for (const auto& room : kh02mirror_metadata::rooms)
    {
        if (room.area != 8 && room.area != 9)
        {
            continue;
        }
        for (int checkpoint : {300, 310, 320, 321, 322, 323, 330, 340, 341, 350, 400, 410, 9999})
        {
            const std::wstring_view tag = kh02_area_resume_tag(room.area, checkpoint, room.starts[0].tag);
            bool found = false;
            for (std::size_t index = 0; index < room.start_count; ++index)
            {
                found |= tag == room.starts[index].tag;
            }
            assert(found);
        }
    }
    assert(kh02_area_entry_checkpoint(8, 310) == 300);
    assert(std::wstring_view(kh02_area_resume_tag(8, 310, L"initial")) == L"dw_08_Lv_Autosave_04");
    for (int checkpoint : {280, 300, 320, 321, 322, 323, 340, 341, 350, 9999})
    {
        assert(kh02_area_entry_checkpoint(8, checkpoint) == checkpoint);
    }
    for (int area : {2, 3, 4, 5, 6, 7, 9, 10})
    {
        assert(kh02_area_entry_checkpoint(area, 310) == 310);
    }
    // A menu snapshot keeps its index mapping while subsequent travel uses current flags.
    flags[kh02_visited_area_flag(2)] = 0;
    assert(selection.selected_area(0) == 2);
    assert(!kh02_visited_area_destination(2, visited));

    // Mirror puzzle rooms record visits, but their scripted arrivals are not
    // save-point destinations. They remain accessible through the hub mirrors.
    flags.clear();
    for (int area : {2, 3, 4, 5, 6, 7})
    {
        flags[kh02_visited_area_flag(area)] = 1;
    }
    const auto save_points = kh02_area_selection(visited);
    assert(save_points.rows.size() == 2);
    assert(save_points.rows[0].text == L"Castle Town");
    assert(save_points.rows[1].text == L"The World Within");
    assert(save_points.selected_area(0) == 2);
    assert(save_points.selected_area(1) == 3);
    for (int area : {4, 5, 6, 7})
    {
        assert(!kh02_visited_area_destination(area, visited));
    }
}
