#pragma once

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <string>
#include <string_view>

namespace kh3ap::save_menu_commands
{
    inline constexpr std::size_t maximum_game_flag_name_length = 128;
    inline auto lower_copy(std::wstring_view value) -> std::wstring
    {
        std::wstring lowered{value};
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](wchar_t ch)
                       {
                           return static_cast<wchar_t>(std::towlower(ch));
                       });
        return lowered;
    }

    inline auto is_save_point_dispatch_command(std::wstring_view command) -> bool
    {
        return lower_copy(command) == L"open_save_point_menu";
    }

    inline auto is_world_map_progress_command(std::wstring_view command) -> bool
    {
        const std::wstring lowered = lower_copy(command);
        return lowered == L"set_world_map_progress" || lowered == L"prepare_world_map";
    }

    inline auto is_set_game_flag_command(std::wstring_view command) -> bool
    {
        const std::wstring lowered = lower_copy(command);
        return lowered == L"set_game_flag" || lowered == L"set_int_game_flag" ||
               lowered == L"set_game_flag_int";
    }

    inline auto is_set_game_flag_boolean_command(std::wstring_view command) -> bool
    {
        const std::wstring lowered = lower_copy(command);
        return lowered == L"set_game_flag_boolean" || lowered == L"set_boolean_game_flag" ||
               lowered == L"set_game_flag_bool";
    }

    inline auto is_get_game_flag_command(std::wstring_view command) -> bool
    {
        const std::wstring lowered = lower_copy(command);
        return lowered == L"get_game_flag" || lowered == L"get_int_game_flag" ||
               lowered == L"get_game_flag_int";
    }

    inline auto is_set_game_flag_label_command(std::wstring_view command) -> bool
    {
        const std::wstring lowered = lower_copy(command);
        return lowered == L"set_game_flag_label" || lowered == L"set_flag_label";
    }

    inline auto is_get_game_flag_boolean_command(std::wstring_view command) -> bool
    {
        const std::wstring lowered = lower_copy(command);
        return lowered == L"get_game_flag_boolean" || lowered == L"get_boolean_game_flag" ||
               lowered == L"get_game_flag_bool";
    }

    inline auto is_valid_game_flag_name(std::wstring_view flag_name) -> bool
    {
        if (flag_name.empty() || flag_name.size() > maximum_game_flag_name_length)
        {
            return false;
        }
        return std::all_of(flag_name.begin(), flag_name.end(),
                           [](wchar_t ch)
                           {
                               return std::iswalnum(ch) != 0 || ch == L'_';
                           });
    }
} // namespace kh3ap::save_menu_commands
