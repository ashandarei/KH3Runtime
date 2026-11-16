#include <array>
#include <cassert>
#include <string>
#include <utility>

constexpr size_t runtime_party_member_slot_count = 2;
struct Property;
struct UObject
{
    std::wstring name;
    bool valid = true;
    Property* property{};
    std::array<UObject*, 6> roster{};
    std::array<UObject*, 2> live{};
    std::wstring GetName() const { return name; }
    Property* GetPropertyByNameInChain(const wchar_t* field)
    {
        assert(std::wstring{field} == L"m_FriendPawnBP");
        return property;
    }
};
struct Property
{
    UObject type{L"ClassProperty"};
    int size = sizeof(UObject*);
    int count = 6;
    UObject& GetClass() { return type; }
    int GetElementSize() { return size; }
    int GetArrayDim() { return count; }
    template <typename T> T* ContainerPtrToValuePtr(UObject* object)
    {
        return object->roster.data();
    }
};
bool is_uobject_valid_fast(UObject* object) { return object && object->valid; }
namespace party_native
{
    bool is_friend_component(UObject* object)
    {
        return is_uobject_valid_fast(object) && object->name == L"manager";
    }
}
UObject* vanilla_friend_manager_{};
std::array<std::wstring, 2> vanilla_npc_ids_{};
#include "party_vanilla_under_test.h"

int main()
{
    Property property;
    UObject donald{L"n_ex001_Pawn_C"}, goofy{L"n_ex002_Pawn_C"};
    UObject xion{L"n_ex033_Pawn_C"}, kairi{L"n_ex005_Pawn_C"};
    UObject manager{L"manager", true, &property, {&donald, &goofy}, {&xion, &kairi}};
    assert(capture_vanilla_party(&manager));
    assert((vanilla_npc_ids_ == std::array<std::wstring, 2>{L"n_ex001", L"n_ex002"}));
    // Repeated native setup with overridden live slots must not poison Vanilla.
    assert(capture_vanilla_party(&manager));
    assert(vanilla_npc_ids_[0] == L"n_ex001");
    UObject incoming = manager;
    assert(capture_vanilla_party(&incoming));
    assert(vanilla_friend_manager_ == &incoming);
    assert(vanilla_npc_ids_[1] == L"n_ex002");
    // Refresh native costumes and preserve intentionally empty logical slots.
    UObject pirate{L"n_ca002_Pawn_C"};
    incoming.roster = {nullptr, &pirate};
    assert(capture_vanilla_party(&incoming));
    assert((vanilla_npc_ids_ == std::array<std::wstring, 2>{L"", L"n_ca002"}));
    const auto previous = vanilla_npc_ids_;
    pirate.valid = false;
    assert(!capture_vanilla_party(&incoming));
    assert(vanilla_npc_ids_ == previous);
    pirate.valid = true;
    pirate.name = L"UnexpectedClass";
    assert(!capture_vanilla_party(&incoming));
    assert(vanilla_npc_ids_ == previous);
    property.type.name = L"ObjectProperty";
    assert(!capture_vanilla_party(&manager));
    property.type.name = L"ClassProperty";
    property.count = 1;
    assert(!capture_vanilla_party(&manager));
    property.count = 6;
    property.size = 4;
    assert(!capture_vanilla_party(&manager));
    manager.property = nullptr;
    assert(!capture_vanilla_party(&manager));
    assert(!capture_vanilla_party(nullptr));
    assert(vanilla_npc_ids_ == previous);
}
