#include "kh3ap/core/input/exit_confirmation.h"

#include <cassert>

int main()
{
    using kh3ap::is_exit_confirmation_dialog;
    assert(is_exit_confirmation_dialog(L"Exit the game?", L"KINGDOM HEARTS III", 4));
    assert(is_exit_confirmation_dialog(L"Exit the game?                                    \n", L"KINGDOM HEARTS III", 0x1104));
    assert(is_exit_confirmation_dialog(L"Exit the game?                                    \nAll unsaved game progress will be lost.           \n", L"KINGDOM HEARTS III", 0x1104));
    assert(is_exit_confirmation_dialog(L"Exit the game?\r\nAll unsaved game progress will be lost.\r\n", L"KINGDOM HEARTS III", 4));
    assert(!is_exit_confirmation_dialog(L"Exit the game?\nAll unsaved game progress will be lost.\nSave failed.", L"KINGDOM HEARTS III", 4));
    assert(!is_exit_confirmation_dialog(L"All unsaved game progress will be lost.", L"KINGDOM HEARTS III", 4));
    assert(!is_exit_confirmation_dialog(L"Exit the game?\nUnsaved data will be lost.", L"KINGDOM HEARTS III", 0x1104));
    assert(!is_exit_confirmation_dialog(L" \n", L"KINGDOM HEARTS III", 4));
    assert(is_exit_confirmation_dialog(L"Exit the game?", L"KINGDOM HEARTS III", 0x124));
    assert(!is_exit_confirmation_dialog(L"Save data is corrupt.", L"KINGDOM HEARTS III", 4));
    assert(!is_exit_confirmation_dialog(L"Exit the game?", L"Other application", 4));
    assert(!is_exit_confirmation_dialog(L"Exit the game?", L"KINGDOM HEARTS III", 3));
    assert(!is_exit_confirmation_dialog(nullptr, L"KINGDOM HEARTS III", 4));
    assert(!is_exit_confirmation_dialog(L"Exit the game?", nullptr, 4));
}
