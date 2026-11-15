#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#define STR(value) L##value
struct UObject;
struct ModeProperty
{
    struct Type { std::wstring_view GetName() { return L"ByteProperty"; } };
    Type GetClass() { return {}; }
    int GetElementSize() { return 1; }
    template <typename T> T* ContainerPtrToValuePtr(UObject* object);
};
ModeProperty mode_property;
struct UObject
{
    bool live = true;
    std::wstring name = L"n_ex011_Pawn_C";
    std::wstring path = L"/Game/Blueprints/Npc/n_ex011/BT/n_ex011_BT.n_ex011_BT";
    UObject* controller{};
    UObject* movement{};
    bool submerged{};
    uint8_t mode = 1;
    UObject* defaults = this;
    UObject* tree{};
    UObject* GetClassPrivate() { return this; }
    UObject* GetClassDefaultObject() { return defaults; }
    const auto& GetName() const { return name; }
    const auto& GetPathName() const { return path; }
    const auto& GetFullName() const { return name; }
    ModeProperty* GetPropertyByNameInChain(std::wstring_view field)
    { return live && field == L"MovementMode" ? &mode_property : nullptr; }
};
template <typename T> T* ModeProperty::ContainerPtrToValuePtr(UObject* object)
{ return reinterpret_cast<T*>(&object->mode); }
bool is_uobject_valid_fast(UObject* object) { return object && object->live; }
struct Weak
{
    UObject* object{};
    UObject* Get() const { return is_uobject_valid_fast(object) ? object : nullptr; }
};
UObject* read_object_like_property(UObject* object, std::wstring_view field)
{
    if (field == L"MyMovement") { return object->movement; }
    return field == L"Controller" ? object->controller : object->tree;
}
auto invoke_named_bool_getter(UObject* object, std::wstring_view field) -> std::optional<bool>
{
    if (!is_uobject_valid_fast(object)) { return std::nullopt; }
    return field == L"IsUnderWater" && object->submerged;
}
#include "party_water_state_under_test.h"
bool write_object_like_property(UObject* object, std::wstring_view, UObject* value)
{
    object->tree = value;
    return true;
}
int runs{};
bool run_ok = true;
bool run_behavior_tree(UObject*, UObject*) { ++runs; return run_ok; }
namespace party_catalog { constexpr size_t aqua_choice = 14; }
struct Definition { size_t choice; };
constexpr std::array<Definition, 2> custom_party_ai_definitions{{{15}, {14}}};
enum class LogLevel { Normal };
struct Output { template <LogLevel, typename... T> static void send(T&&...) {} };
struct Service
{
    std::array<Weak, 2> custom_party_assigned_pawns_{};
    int assignments{};
    bool apply_custom_party_ai_for_possession(UObject*, UObject* pawn)
    {
        ++assignments;
        custom_party_assigned_pawns_[1] = {pawn};
        return true;
    }
#include "party_swim_ai_under_test.h"
};
int main()
{
    UObject pawn, controller, defaults, stock, movement;
    pawn.movement = &movement;
    assert(friend_underwater(&pawn) == false);
    // Locomotion can stop swimming during an attack while movement stays submerged.
    movement.submerged = true;
    movement.mode = 4;
    assert(invoke_named_bool_getter(&pawn, L"IsSwimUnderWater") == false);
    assert(friend_underwater(&pawn) == true);
    movement.mode = 5;
    assert(!friend_underwater(&pawn).has_value()); // Air attack must not restore land state.
    movement.mode = 3;
    assert(!friend_underwater(&pawn).has_value()); // Falling cannot establish an exit either.
    movement.mode = 4;
    movement.submerged = false;
    assert(friend_underwater(&pawn) == false); // Surface swimming confirms an exit.
    movement.mode = 1;
    assert(friend_underwater(&pawn) == false);
    movement.live = false;
    assert(!friend_underwater(&pawn).has_value());
    movement.live = true;
    pawn.controller = &controller;
    pawn.defaults = &defaults;
    defaults.tree = &stock;
    Service service;
    // Initial dry spawning leaves the existing native AI completely untouched.
    assert(service.update_swimming_party_ai(&pawn, false));
    assert(runs == 0 && service.assignments == 0);
    assert(service.update_swimming_party_ai(&pawn, true));
    assert(service.update_swimming_party_ai(&pawn, true));
    assert(service.assignments == 1 && runs == 0);
    // Failed restoration keeps ownership so it can be recovered, never reports success.
    stock.live = false;
    assert(!service.update_swimming_party_ai(&pawn, false));
    assert(service.custom_party_assigned_pawns_[1].Get() == &pawn && runs == 0);
    stock.live = true;
    run_ok = false;
    assert(!service.update_swimming_party_ai(&pawn, false));
    assert(service.custom_party_assigned_pawns_[1].Get() == &pawn);
    run_ok = true;
    assert(service.update_swimming_party_ai(&pawn, false));
    assert(pawn.tree == &stock && !service.custom_party_assigned_pawns_[1].Get());
    const int restored_runs = runs;
    assert(service.update_swimming_party_ai(&pawn, false));
    assert(runs == restored_runs);
    UObject replacement;
    replacement.controller = &controller;
    service.custom_party_assigned_pawns_[1] = {&pawn};
    assert(service.update_swimming_party_ai(&replacement, false));
    assert(runs == restored_runs); // A replacement never restores the old pawn's tree.
    controller.live = false;
    assert(!service.update_swimming_party_ai(&replacement, true));
    replacement.name = L"n_ex006_Pawn_C";
    assert(service.update_swimming_party_ai(&replacement, true));
}
