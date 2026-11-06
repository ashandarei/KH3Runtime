#pragma once
#include <string>
#include <string_view>

namespace kh3ap
{
    inline constexpr auto kh02_mirror_common_entry = L"dw_03_PersistentLevel_REvt_LevelStart";

    inline auto kh02_field_host_entry_fade(std::wstring_view world) -> bool
    {
        return world == L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest" ||
               world == L"/Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths";
    }

    struct Kh02MirrorArrival
    {
        std::wstring event;
        bool common_entry_required{};
        bool host_entry_fade{};
    };

    inline auto kh02_mirror_arrival(bool resume, std::wstring_view tag) -> Kh02MirrorArrival
    {
        std::wstring event =
            resume ? L"dw_03_PersistentLevel_REvt_LevelStart" : L"dw_03_PersistentLevel_REvt_LevelStart_dw015";
        if (!tag.empty())
        {
            event = L"dw_03_PersistentLevel_REvt_LevelStart";
            for (int area : {4, 5, 6})
            {
                if (tag == L"dw_03_Lv_Clear_dw_0" + std::to_wstring(area))
                {
                    event += L"_dw_0" + std::to_wstring(area) + L"_Clear";
                }
            }
            if (tag == L"dw_03_Lv_ChallengeClear")
            {
                event = L"dw_03_PersistentLevel_REvt_LevelStart_ChallengeEnd";
            }
        }

        const bool common_required =
            event != kh02_mirror_common_entry && event != L"dw_03_PersistentLevel_REvt_LevelStart_dw015";
        const bool host_entry_fade = event == kh02_mirror_common_entry;
        return {std::move(event), common_required, host_entry_fade};
    }
} // namespace kh3ap
