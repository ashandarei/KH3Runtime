#pragma once
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"

namespace kh3ap
{
    inline auto kh02_area_entry_checkpoint(int area, int checkpoint) -> int
    {
        // DW022 records 310 before its movie and 320 when combat starts.
        // Reloading 310 neither retries the boss nor permits its <310 trigger.
        // Replay that unfinished introduction from the preceding checkpoint.
        if (area == 8 && checkpoint == kh02_checkpoint::forest_scene_dw022)
        {
            return kh02_checkpoint::forest_scene_dw021;
        }
        if (area == 8 && checkpoint == kh02_checkpoint::forest_scene_dw023)
        {
            return kh02_checkpoint::forest_area_f_end;
        }
        return checkpoint;
    }

    inline auto kh02_area_resume_tag(int area, int checkpoint, const wchar_t* initial_tag) -> const wchar_t*
    {
        // The source level chooses its retry event from story progress, but
        // chooses the streamed encounter area from the arrival tag. Keep the
        // two in agreement on re-entry. Once an encounter is complete, use
        // its authored continuation or the latest unlocked save point.
        if (area == 9 && checkpoint >= kh02_checkpoint::forest_scene_dw026)
        {
            return L"dw_09_Lv_Save_01";
        }
        if (area != 8)
        {
            return initial_tag;
        }
        if (checkpoint >= kh02_checkpoint::forest_scene_dw024)
        {
            return L"dw_08_Lv_Save_03";
        }
        if (checkpoint >= kh02_checkpoint::second_darkside_end)
        {
            return L"dw_08_Lv_Start_03";
        }
        if (checkpoint >= kh02_checkpoint::second_darkside_start)
        {
            return L"dw_08_Lv_Retry_02";
        }
        if (checkpoint >= kh02_checkpoint::forest_area_f_end)
        {
            return L"dw_08_Lv_Save_02";
        }
        if (checkpoint == kh02_checkpoint::forest_area_f_start)
        {
            return L"dw_08_Lv_Retry_03";
        }
        if (checkpoint == kh02_checkpoint::first_darkside_end)
        {
            return L"dw_08_Lv_Autosave_05";
        }
        if (checkpoint == kh02_checkpoint::first_darkside_start)
        {
            return L"dw_08_Lv_Retry_01";
        }
        if (checkpoint >= kh02_checkpoint::forest_scene_dw022)
        {
            return L"dw_08_Lv_Autosave_04";
        }
        if (checkpoint >= kh02_checkpoint::forest_scene_dw021)
        {
            return L"dw_08_Lv_Save_01";
        }
        return initial_tag;
    }

} // namespace kh3ap
