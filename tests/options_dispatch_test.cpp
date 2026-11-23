#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/input/title_shortcut.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#define STR(value) L##value
constexpr int FNAME_Add = 0;
constexpr int input_event_released = 1;
struct FName
{
    std::wstring value;
    FName() = default;
    FName(const wchar_t* text, int = 0) : value(text) {}
    std::wstring ToString() const { return value; }
};
struct RawFKey { FName key_name; };
using ViewportInputKeyFn = bool (*)(void*, void*, int32_t, RawFKey*, int32_t, float, bool);
bool game_window_is_foreground() { return true; }
int title_requests{};
bool title_available{true};
bool return_to_title(const wchar_t*) { ++title_requests; return title_available; }
using DWORD = uint32_t;
using UINT = unsigned int;
constexpr int XUSER_MAX_COUNT = 4, ERROR_SUCCESS = 0;
constexpr int XINPUT_GAMEPAD_BACK = 0x20, XINPUT_GAMEPAD_START = 0x10;
constexpr int XINPUT_GAMEPAD_LEFT_SHOULDER = 0x100, XINPUT_GAMEPAD_RIGHT_SHOULDER = 0x200;
struct XINPUT_STATE { struct { uint16_t wButtons{}; } Gamepad; };
std::array<std::optional<uint16_t>, XUSER_MAX_COUNT> xinput_buttons{};
std::array<int, XUSER_MAX_COUNT> xinput_reads{};
auto loaded_xinput_get_state()
{
    return +[](DWORD slot, XINPUT_STATE* state)
    {
        ++xinput_reads.at(slot);
        if (!xinput_buttons.at(slot)) { return 1167; }
        state->Gamepad.wButtons = *xinput_buttons.at(slot);
        return ERROR_SUCCESS;
    };
}
constexpr int JOYERR_NOERROR = 0, JOY_RETURNBUTTONS = 128;
constexpr DWORD JOY_BUTTON5 = 0x10, JOY_BUTTON6 = 0x20, JOY_BUTTON9 = 0x100, JOY_BUTTON10 = 0x200;
struct JOYCAPSW { int wMid, wPid; };
struct JOYINFOEX { DWORD dwSize, dwFlags, dwButtons; };
std::array<DWORD, 2> device_buttons{};
int device_reads{};
UINT joyGetNumDevs() { return static_cast<UINT>(device_buttons.size()); }
int joyGetDevCapsW(UINT, JOYCAPSW* caps, size_t) { *caps = {0x054c, 0x0ce6}; return 0; }
int joyGetPosEx(UINT id, JOYINFOEX* state)
{
    ++device_reads;
    state->dwButtons = device_buttons.at(id);
    return 0;
}
std::wstring native_key;
bool native_input(void*, void*, int32_t, RawFKey* key, int32_t, float, bool)
{
    native_key = key->key_name.ToString();
    return false;
}
struct RuntimeOptionsMenuService
{
    struct Impl
    {
        struct Context { void* value{}; void* Get() const { return value; } } menu_context_;
        struct Shortcut { bool route(int, std::wstring, int) { return false; } } settings_shortcut_;
        kh3ap::TitleShortcut title_shortcut_;
        std::optional<std::vector<UINT>> dualsense_devices_;
        void check_controller_title_shortcut();
        kh3ap::RuntimeOptionsInputOwnership title_keyboard_input_;
        struct Config { std::function<bool(int, std::wstring_view, int, bool)> gameplay_key; } config_;
        std::atomic<uintptr_t> s_original_input_key_{reinterpret_cast<uintptr_t>(&native_input)};
        kh3ap::RuntimeOptionsInputOwnership gameplay_input_ownership_;
        bool custom_menu_consumes{};
        bool block_background_controller_input() { return false; }
        bool route_viewport_key(const FName&, int, int, bool) { return custom_menu_consumes; }
        bool dispatch_key(void*, void*, int32_t, RawFKey*, int32_t, float, bool);
    };
};
#include "options_dispatch_under_test.h"

int main()
{
    RuntimeOptionsMenuService::Impl host;
    int presses = 0, releases = 0;
    bool dash = false;
    bool swimming = true;
    kh3ap::RuntimeOptionsInputOwnership swim_input;
    host.config_.gameplay_key = [&](int controller, std::wstring_view key, int event, bool)
    {
        return swim_input.route(controller, std::wstring(key), event, [&]
        {
            if (event == 1) { ++releases; dash = false; return true; }
            if (!swimming) { return false; }
            ++presses;
            dash = true;
            return true;
        });
    };
    const auto send = [&](int event)
    {
        RawFKey key{FName{L"Gamepad_FaceButton_Right"}};
        return host.dispatch_key(nullptr, nullptr, 0, &key, event, 1.0f, true);
    };

    // Back on the stock settings page reaches the native menu, never dash.
    host.menu_context_.value = &host;
    assert(!send(0));
    assert(native_key == L"Gamepad_FaceButton_Right");
    assert(!dash && presses == 0);
    // Closing the menu while Back is held must not start gameplay on repeat/release.
    host.menu_context_.value = nullptr;
    assert(!send(2));
    assert(!send(1));
    assert(presses == 0 && releases == 0);

    // Normal underwater dash resumes once the menu is closed.
    assert(send(0) && dash && presses == 1 && native_key.empty());
    host.menu_context_.value = &host;
    host.custom_menu_consumes = true;
    assert(send(2) && presses == 1);
    assert(send(1) && !dash && releases == 1);
    assert(native_key.empty());

    // Custom options retain their own press; no gameplay release is synthesized.
    assert(send(0));
    host.custom_menu_consumes = false;
    assert(!send(1));
    assert(presses == 1 && releases == 1);
    assert(!send(1));
    assert(releases == 1);

    // A rejected gameplay press still needs its release in the downstream
    // ownership table. Otherwise pressing Back on land permanently blocks dash.
    host.menu_context_.value = nullptr;
    swimming = false;
    assert(!send(0));
    assert(!send(1));
    swimming = true;
    assert(send(0) && dash && presses == 2);
    assert(send(1) && !dash && releases == 2);

    host.config_.gameplay_key = {};
    host.menu_context_.value = &host;
    const auto button = [&](const wchar_t* name, int event, int pad = 0, bool gamepad = true)
    {
        RawFKey key{FName{name}};
        return host.dispatch_key(nullptr, nullptr, pad, &key, event, 1.0f, gamepad);
    };
    std::array<const wchar_t*, 4> chord{L"Gamepad_Special_Left", L"Gamepad_Special_Right",
                                        L"Gamepad_LeftShoulder", L"Gamepad_RightShoulder"};
    std::array<int, 4> order{0, 1, 2, 3};
    do
    {
        const int before = title_requests;
        for (int i = 0; i < 3; ++i)
        {
            assert(!button(chord[order[i]], 0));
            assert(native_key == chord[order[i]]);
        }
        const auto* last = chord[order.back()];
        assert(!button(last, 0, 1));
        assert(!button(last, 1, 1));
        assert(!button(last, 0, 0, false));
        assert(!button(last, 1, 0, false));
        assert(!button(last, 2));
        assert(title_requests == before);
        assert(button(last, 0) && native_key.empty());
        assert(button(last, 0) && button(last, 2));
        assert(button(last, 1) && native_key.empty());
        assert(!button(last, 0) && !button(last, 1));
        for (int i = 0; i < 3; ++i)
        {
            assert(!button(chord[order[i]], 1));
            assert(native_key == chord[order[i]]);
        }
        assert(title_requests == before + 1);
    } while (std::next_permutation(order.begin(), order.end()));

    title_available = false;
    for (auto key : chord) { assert(!button(key, 0) && native_key == key); }
    for (auto key : chord) { assert(!button(key, 1) && native_key == key); }
    assert(title_requests == 25);

    title_available = true;
    assert(button(L"F12", 0, 0, false));
    assert(button(L"F12", 2, 0, false));
    assert(button(L"F12", 1, 0, false));
    assert(title_requests == 26 && native_key.empty());
    title_available = false;
    assert(!button(L"F12", 0, 0, false) && native_key == L"F12");
    assert(!button(L"F12", 1, 0, false));
    assert(title_requests == 27);

    assert(!button(chord[2], 0));
    assert(!host.title_shortcut_.awaiting_create());
    assert(!button(chord[1], 0));
    assert(host.title_shortcut_.awaiting_create() == 0);
    assert(!button(chord[3], 0));
    assert(host.title_shortcut_.awaiting_create() == 0);
    title_available = true;
    device_buttons = {0x300, 0x30};
    host.check_controller_title_shortcut();
    assert(title_requests == 27);
    device_buttons[1] = 0x330;
    host.check_controller_title_shortcut();
    assert(title_requests == 28);
    assert(!host.title_shortcut_.awaiting_create());
    assert(!button(chord[0], 0));
    for (auto key : chord) { assert(!button(key, 1)); }
    const int reads_before_idle = device_reads;
    host.check_controller_title_shortcut();
    assert(device_reads == reads_before_idle && title_requests == 28);

    // Steam Input's virtual pad and a native Xbox pad report the same
    // XInput buttons. The menu can take over the remaining viewport presses.
    device_buttons = {};
    xinput_buttons[1] = uint16_t{0x30};
    xinput_buttons[2] = uint16_t{0x300};
    assert(!button(chord[1], 0, 1) && !button(chord[1], 0, 2));
    host.check_controller_title_shortcut();
    assert(title_requests == 28);
    xinput_buttons[2] = uint16_t{0x330};
    host.check_controller_title_shortcut();
    host.check_controller_title_shortcut();
    assert(title_requests == 29 && xinput_reads[2] == 2);
    assert(!button(chord[1], 1, 1) && !button(chord[1], 1, 2));
    const auto idle_reads = xinput_reads;
    host.check_controller_title_shortcut();
    assert(xinput_reads == idle_reads);

    // A disconnected slot is probed once per Menu hold and can reconnect.
    assert(!button(chord[1], 0, 3));
    host.check_controller_title_shortcut();
    host.check_controller_title_shortcut();
    assert(xinput_reads[3] == 1);
    assert(!button(chord[1], 1, 3));
    xinput_buttons[3] = uint16_t{0x330};
    assert(!button(chord[1], 0, 3));
    host.check_controller_title_shortcut();
    assert(title_requests == 30 && xinput_reads[3] == 2);
    assert(!button(chord[1], 1, 3));
}
