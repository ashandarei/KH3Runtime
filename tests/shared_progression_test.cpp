#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "kh3ap/features/player/state/shared_sora_progression.h"

namespace RC::Unreal
{
    struct Property
    {
        std::wstring type{L"IntProperty"};
        int offset{8};
        auto GetClass() -> Property& { return *this; }
        auto GetName() -> std::wstring { return type; }
        auto GetElementSize() -> int { return 4; }
        auto GetArrayDim() -> int { return 1; }
        auto GetOffset_Internal() -> int { return offset; }
        template<class T> auto ContainerPtrToValuePtr(void* value) -> T* { return static_cast<T*>(value); }
    };
    class UObject
    {
    public:
        bool valid{true};
        int serial{};
        Property property;
        std::map<std::wstring, UObject*> fields;
        auto GetPropertyByNameInChain(const wchar_t*) -> Property* { return &property; }
    };
    struct FWeakObjectPtr
    {
        UObject* value{};
        int serial{};
        auto Get() const -> UObject* { return value && value->valid && value->serial == serial ? value : nullptr; }
    };
}
using namespace RC::Unreal;
UObject pawn, controller, player, owner, table;
std::array<int,99> rows{};
bool missing_row{}, replace_owner{};
std::map<uintptr_t,std::vector<std::byte>> memory;
auto is_uobject_valid_fast(UObject* value) -> bool { return value && value->valid; }
auto make_uobject_weak(UObject* value) -> FWeakObjectPtr { return {value,value->serial}; }
auto try_read_reflected_object_property_value(UObject* object,const wchar_t* key) -> UObject*
{ return object && object->valid ? object->fields[key] : nullptr; }
auto find_loaded_runtime_asset(const wchar_t*) -> UObject* { return &table; }
auto accessor(UObject*) -> UObject* { return replace_owner ? nullptr : &owner; }
auto GetModuleHandleW(const wchar_t*) -> void* { return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(&accessor)-0x6f8a60); }
namespace kh3ap
{
    namespace character_native { enum class Build { Unsupported, Egs, Steam }; auto build() -> Build { return Build::Egs; } }
    auto try_read_process_memory_bytes(uintptr_t address,size_t size) -> std::optional<std::vector<std::byte>>
    { auto it=memory.find(address); return it!=memory.end() && it->second.size()==size ? std::optional{it->second} : std::nullopt; }
    struct Kh02DataTableView
    {
        UObject* schema;
        Kh02DataTableView(UObject* object,const wchar_t*,const char*) : schema(object) {}
        auto find(const wchar_t* name) -> void* { return missing_row ? nullptr : &rows.at(std::stoi(name)-1); }
    };
}
template<class T> void store(uintptr_t address,T value)
{ std::vector<std::byte> bytes(sizeof(T)); std::memcpy(bytes.data(),&value,sizeof(T)); memory[address]=bytes; }
#include "shared_progression_under_test.h"

int main()
{
    pawn.fields[L"Controller"]=&controller;
    controller.fields[L"Pawn"]=&pawn;
    controller.fields[L"Player"]=&player;
    player.fields[L"PlayerController"]=&controller;
    const auto address=reinterpret_cast<uintptr_t>(&pawn);
    const auto owner_address=reinterpret_cast<uintptr_t>(&owner);
    store<uintptr_t>(address,0x10000);
    store<uintptr_t>(0x108c0,reinterpret_cast<uintptr_t>(&accessor));
    store<int>(address+0x514,0);
    constexpr unsigned char signature[]{0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xff,0x90,0x40,0x01,0x00,0x00,0x48,0x85,0xc0};
    auto& code=memory[reinterpret_cast<uintptr_t>(&accessor)];
    code.resize(sizeof(signature)); std::memcpy(code.data(),signature,sizeof(signature));
    for(size_t i=0;i<rows.size();++i) rows[i]=static_cast<int>(i)*100;
    // No Sora pawn or alternate base stats exist in this fixture: only shared EXP.
    for(auto [exp,level]:std::array<std::pair<int,int>,6>{{{0,1},{399,4},{400,5},{499,5},{500,6},{9999999,99}}})
    {
        store(owner_address+0x8670,exp);
        auto snapshot=kh3ap::read_shared_sora_progression(&pawn);
        assert(snapshot && snapshot->experience==exp && snapshot->level==level);
    }
    controller.fields[L"Pawn"]=nullptr; // Parked/companion pawn must not admit reads.
    assert(!kh3ap::read_shared_sora_progression(&pawn));
    controller.fields[L"Pawn"]=&pawn;
    store<int>(address+0x514,1); assert(!kh3ap::read_shared_sora_progression(&pawn));
    store<int>(address+0x514,0);
    store(owner_address+0x8670,-1); assert(!kh3ap::read_shared_sora_progression(&pawn));
    store(owner_address+0x8670,500);
    owner.valid=false; assert(!kh3ap::read_shared_sora_progression(&pawn)); owner.valid=true;
    ++table.serial; rows[5]=1; assert(!kh3ap::read_shared_sora_progression(&pawn)); rows[5]=500;
    missing_row=true; assert(!kh3ap::read_shared_sora_progression(&pawn)); missing_row=false;
    table.property.type=L"ByteProperty"; assert(!kh3ap::read_shared_sora_progression(&pawn));
    table.property.type=L"IntProperty";
    assert(kh3ap::read_shared_sora_progression(&pawn)->level==6);
    code[0]=std::byte{}; assert(!kh3ap::read_shared_sora_progression(&pawn));
}
