#include "kh3ap/core/input/menu_face_button.h"
#include "kh3ap/core/input/options_input_ownership.h"

#include <cassert>
#include <string>

int main()
{
    using namespace kh3ap;
    using Action = RuntimeMenuFaceButton;
    constexpr auto bottom = L"Gamepad_FaceButton_Bottom";
    constexpr auto right = L"Gamepad_FaceButton_Right";

    assert(menu_face_button(bottom, true) == Action::confirm);
    assert(menu_face_button(right, true) == Action::back);
    assert(menu_face_button(bottom, false) == Action::back);
    assert(menu_face_button(right, false) == Action::confirm);
    for (const auto key : {bottom, right})
    {
        assert(menu_face_button(key, std::nullopt) == Action::none);
    }
    for (const auto key : {L"Enter", L"Escape", L"Virtual_Accept", L"Virtual_Back",
                           L"LeftMouseButton", L"Gamepad_FaceButton_Top", L"Gamepad_DPad_Down"})
    {
        assert(!is_menu_face_button(key));
        assert(menu_face_button(key, true) == Action::none);
        assert(menu_face_button(key, false) == Action::none);
    }

    // A swapped Back returning to native UI must still consume its release,
    // even if the setting or focus changes while the physical button is held.
    RuntimeOptionsInputOwnership ownership;
    bool exchange = false;
    bool custom_page = true;
    int backs = 0;
    int confirms = 0;
    const auto route = [&](const std::wstring& key, int event)
    {
        return ownership.route(0, key, event,
                               [&]()
                               {
                                   if (!custom_page || event != 0)
                                   {
                                       return false;
                                   }
                                   const auto action = menu_face_button(key, exchange);
                                   if (action == Action::back)
                                   {
                                       ++backs;
                                       custom_page = false;
                                   }
                                   if (action == Action::confirm)
                                   {
                                       ++confirms;
                                   }
                                   return action != Action::none;
                               });
    };
    assert(route(bottom, 0));
    assert(backs == 1 && confirms == 0);
    exchange = true;
    assert(route(bottom, 2));
    assert(route(bottom, 1));
    custom_page = true;
    assert(route(bottom, 0));
    assert(confirms == 1);
    assert(route(bottom, 1));
    exchange = false;
    assert(route(right, 0));
    assert(confirms == 2);
    assert(route(right, 1));
    assert(backs == 1);
}
