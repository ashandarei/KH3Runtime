#pragma once
#include <string_view>

namespace kh3ap
{
    constexpr bool supports_alternate_game_over(std::wstring_view action, std::wstring_view token,
                                                std::wstring_view method)
    {
        return action == L"manual_game_over" && token == L"GAMEOVER_KIND_NORMAL" &&
               method == L"bp_tres_start_game_over";
    }
}
