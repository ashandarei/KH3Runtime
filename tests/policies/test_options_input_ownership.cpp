#include "kh3ap/core/input/options_input_ownership.h"

#include <cassert>

int main()
{
    kh3ap::RuntimeOptionsInputOwnership input;
    bool custom_focus = false;
    int dispatches = 0;
    const auto dispatch = [&]()
    {
        ++dispatches;
        return custom_focus;
    };

    // A native direction held while opening AP Options still gets its release.
    assert(!input.route(0, L"Gamepad_LeftStick_Down", 0, dispatch));
    custom_focus = true;
    assert(!input.route(0, L"Gamepad_LeftStick_Down", 2, dispatch));
    assert(!input.route(0, L"Gamepad_LeftStick_Down", 1, dispatch));
    assert(dispatches == 1);

    // Custom navigation and confirm overlap; neither can steal the other's
    // release ownership, even after returning to native Config or closing it.
    assert(input.route(0, L"Gamepad_DPad_Down", 0, dispatch));
    assert(input.route(0, L"Gamepad_FaceButton_Bottom", 0, dispatch));
    custom_focus = false;
    const int before_repeat = dispatches;
    assert(input.route(0, L"Gamepad_DPad_Down", 0, dispatch));
    assert(input.route(0, L"Gamepad_DPad_Down", 2, dispatch));
    assert(dispatches == before_repeat);
    assert(input.route(0, L"Gamepad_DPad_Down", 1, dispatch));
    assert(input.route(0, L"Gamepad_FaceButton_Bottom", 1, dispatch));
    assert(!input.route(0, L"Gamepad_DPad_Down", 0, dispatch));
    assert(!input.route(0, L"Gamepad_DPad_Down", 1, dispatch));

    // Controllers and virtual aliases have separate native press lifecycles.
    assert(!input.route(1, L"Virtual_Up", 0, dispatch));
    custom_focus = true;
    assert(input.route(0, L"Virtual_Up", 0, dispatch));
    assert(input.route(0, L"Gamepad_LeftStick_Up", 0, dispatch));
    assert(!input.route(1, L"Virtual_Up", 1, dispatch));
    assert(input.route(0, L"Virtual_Up", 1, dispatch));
    assert(input.route(0, L"Gamepad_LeftStick_Up", 1, dispatch));

    // Orphan releases/repeats must never become fresh custom presses.
    const int before_orphan = dispatches;
    assert(!input.route(0, L"Up", 1, dispatch));
    assert(!input.route(0, L"Up", 2, dispatch));
    assert(dispatches == before_orphan);
}
