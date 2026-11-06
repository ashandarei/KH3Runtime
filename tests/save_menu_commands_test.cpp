#include "kh3ap/features/story/save_points/save_menu_commands.h"
#include <cassert>
#include <string>

int main()
{
    using namespace kh3ap::save_menu_commands;
    assert(lower_copy(L"GameFlow_KG") == L"gameflow_kg");
    assert(is_save_point_dispatch_command(L"OPEN_SAVE_POINT_MENU"));
    for (const auto retired : {L"open", L"open_save_menu", L"open_save_selection_menu", L""})
    {
        assert(!is_save_point_dispatch_command(retired));
    }
    assert(is_world_map_progress_command(L"prepare_world_map"));
    assert(is_world_map_progress_command(L"set_world_map_progress"));
    assert(is_set_game_flag_command(L"SET_GAME_FLAG"));
    assert(is_set_game_flag_command(L"set_int_game_flag"));
    assert(is_set_game_flag_command(L"set_game_flag_int"));
    assert(!is_set_game_flag_command(L"get_game_flag"));
    assert(is_set_game_flag_boolean_command(L"set_boolean_game_flag"));
    assert(is_set_game_flag_boolean_command(L"set_game_flag_bool"));
    assert(is_set_game_flag_boolean_command(L"set_game_flag_boolean"));
    assert(is_get_game_flag_command(L"get_game_flag_int"));
    assert(is_get_game_flag_command(L"get_int_game_flag"));
    assert(is_get_game_flag_command(L"get_game_flag"));
    assert(is_set_game_flag_label_command(L"set_flag_label"));
    assert(is_set_game_flag_label_command(L"set_game_flag_label"));
    assert(is_get_game_flag_boolean_command(L"get_boolean_game_flag"));
    assert(is_get_game_flag_boolean_command(L"get_game_flag_boolean"));
    assert(is_get_game_flag_boolean_command(L"get_game_flag_bool"));
    assert(!is_get_game_flag_boolean_command(L"set_game_flag_bool"));
    assert(is_valid_game_flag_name(L"gameflow_HE"));
    assert(is_valid_game_flag_name(std::wstring(128, L'A')));
    assert(!is_valid_game_flag_name(std::wstring(129, L'A')));
    assert(!is_valid_game_flag_name(L""));
    assert(!is_valid_game_flag_name(L"gameflow HE"));
    assert(!is_valid_game_flag_name(L"../gameflow_HE"));
}
