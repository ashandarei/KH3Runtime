#include "kh3ap/core/input/options_input_ownership.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

bool camp_open = false;
struct RuntimeGardenReturnService { static bool camp_menu_open() { return camp_open; } };
struct Property
{
    int phase = 4;
    int GetElementSize() const { return sizeof(int32_t); }
    template<typename T> T* ContainerPtrToValuePtr(void*) { return reinterpret_cast<T*>(&phase); }
} swim_phase;
struct UObject
{
    UObject* GetClassPrivate() { return this; }
    std::wstring GetName() const { return L"TresPlayerState_Swim"; }
    Property* GetPropertyByNameInChain(const wchar_t*) { return &swim_phase; }
} hero;
struct Weak { UObject* value{}; UObject* Get() const { return value; } } swim_input_pawn;
Weak make_uobject_weak(UObject* p) { return {p}; }
namespace character_swap
{
    UObject* current_hero() { return &hero; }
    bool owns_swap_pawn(UObject* p) { return p == &hero; }
}
bool ready(UObject* p) { return p == &hero; }
bool is_uobject_valid_fast(UObject* p) { return p != nullptr; }
UObject* object(UObject* p, const wchar_t*) { return p; }
kh3ap::RuntimeOptionsInputOwnership swim_input;
bool descend_held{}, ascend_held{}, dash_held{}, dive_requested{};
#include "swim_input_under_test.h"

int main()
{
    constexpr auto back = L"Gamepad_FaceButton_Right";
    assert(route(0, back, 0, true) && dash_held);
    // Opening camp while holding dash still permits cleanup of that press.
    camp_open = true;
    assert(route(0, back, 1, true) && !dash_held);
    // Camp and its submenus keep native Back while Swim remains underneath.
    assert(!route(0, back, 0, true) && !dash_held);
    camp_open = false;
    assert(!route(0, back, 2, true));
    assert(!route(0, back, 1, true));
    assert(route(0, back, 0, true) && dash_held);
    assert(route(0, back, 1, true) && !dash_held);
    // Menu trigger presses cannot latch ascend/descend either.
    camp_open = true;
    for (const auto* key : {L"Gamepad_LeftTrigger", L"Gamepad_RightTrigger"})
    {
        assert(!route(0, key, 0, true));
        assert(!route(0, key, 1, true));
    }
    assert(!descend_held && !ascend_held);
    camp_open = false;
    assert(route(0, L"Gamepad_RightTrigger", 0, true) && ascend_held);
    assert(route(0, L"Gamepad_RightTrigger", 1, true) && !ascend_held);
}
