#include "kh3ap/core/input/options_input_ownership.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <string>
#include <string_view>

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
}
