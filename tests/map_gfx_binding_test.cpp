#include "map_gfx_binding_under_test.h"
#include <array>
#include <cassert>

using namespace kh3ap;
using namespace RC::Unreal;
namespace {
    int field_lookups{}, param_lookups{}, serial_allocations{};
    auto register_object(UObject& object,UClass* type,int serial=1) -> void {
        object.type=type; object.index=UObjectArray::GetNumElements();
        UObjectArray::items.push_back({&object,true,serial});
    }
    auto property(FProperty& field,const wchar_t* type,int offset,int bytes) -> void {
        field.type.name=type; field.offset=offset; field.size=bytes;
    }
}
auto kh3ap::find_struct_field_by_name(UScriptStruct* type,std::wstring_view name) -> FProperty* {
    ++field_lookups;
    const auto found=type->fields.find(std::wstring{name});
    return found==type->fields.end()?nullptr:found->second;
}
auto kh3ap::find_function_param_by_name(UFunction* fn,std::wstring_view name) -> FProperty* {
    ++param_lookups;
    const auto found=fn->parameters.find(std::wstring{name});
    return found==fn->parameters.end()?nullptr:found->second;
}
auto kh3ap::make_uobject_weak(UObject* object) -> FWeakObjectPtr {
    FWeakObjectPtr ref;
    if (!is_uobject_valid_fast(object)) return ref;
    if (UObjectArray::items[object->index].serial==0) {
        UObjectArray::items[object->index].serial=100+(++serial_allocations);
    }
    ref=object;
    return ref;
}
int main() {
    UClass meta,root,ui_type,function_type,struct_type;
    UClass::class_type=&meta; UFunction::function_type=&function_type;
    UScriptStruct::struct_type=&struct_type;
    for (auto* type:{&meta,&root,&ui_type,&function_type,&struct_type}) register_object(*type,&meta);
    ui_type.parent=function_type.parent=struct_type.parent=&root;
    UObject owner; register_object(owner,&ui_type);
    UScriptStruct as_value; register_object(as_value,&struct_type,0);
    FProperty tag,integer,number,object;
    FBoolProperty flag;
    property(tag,L"ByteProperty",0,1); property(integer,L"IntProperty",8,4);
    property(number,L"DoubleProperty",16,8); property(object,L"ObjectProperty",32,8);
    property(flag,L"BoolProperty",4,1); flag.mask=0x10;
    as_value.fields={{L"ASType",&tag},{L"ASInt",&integer},{L"ASNumber",&number},
                     {L"ASObject",&object},{L"ASBoolean",&flag}};
    UFunction getter; register_object(getter,&function_type,0); getter.size=72;
    FProperty member;
    property(member,L"StrProperty",0,16);
    FStructProperty returned;
    property(returned,L"StructProperty",16,56); returned.structure=&as_value;
    getter.parameters={{L"MemberName",&member},{L"ReturnValue",&returned}};
    owner.functions={{L"GetMemberValue",&getter}};
    GfxBinding binding;
    assert(binding.resolve(&owner,L"/Script/ScaleformUI.GFxObject:GetMemberValue",false)==&getter);
    assert(serial_allocations==2 && binding.return_value.valid());
    const int fields=field_lookups,params=param_lookups,functions=owner.function_lookups;
    for (int i=0;i<100;++i)
        assert(binding.resolve(&owner,L"/Script/ScaleformUI.GFxObject:GetMemberValue",false)==&getter);
    assert(field_lookups==fields && param_lookups==params && owner.function_lookups==functions);

    std::array<std::byte,72> buffer{};
    assert(binding.borrow_name(buffer.data(),L"visible"));
    const auto* name=reinterpret_cast<BorrowedString*>(buffer.data());
    assert(std::wstring{name->data}==L"visible" && name->count==8);
    auto* value=buffer.data()+16;
    // Live visibility/completion values must not be frozen by metadata reuse.
    assert(binding.return_value.write_bool(value,true));
    assert(binding.return_value.read_bool(value)==1);
    assert(binding.return_value.write_bool(value,false));
    assert(binding.return_value.read_bool(value)==0);
    assert(binding.return_value.write_number(value,1));
    assert(binding.return_value.number(value)==1);
    assert(binding.return_value.write_number(value,0));
    assert(binding.return_value.number(value)==0); // removed marker alpha
    UObject first,second;
    register_object(first,&root); register_object(second,&root);
    assert(binding.return_value.write_object(value,&first) && binding.return_value.object(value)==&first);
    assert(binding.return_value.write_object(value,&second) && binding.return_value.object(value)==&second);

    // A just-created native movie may have no weak serial. Keep the exact
    // returned identity without a path search, reject slot reuse, then reopen.
    UObject movie; register_object(movie,&ui_type,0);
    MapMovieBinding player;
    assert(player.remember(&movie) && player.get()==&movie);
    const int assigned=serial_allocations;
    for (int i=0;i<100;++i) assert(player.get()==&movie);
    assert(serial_allocations==assigned);
    ++UObjectArray::items[movie.index].serial;
    assert(!player.get());
    assert(player.remember(&second) && player.get()==&second);
    player.reset(); assert(!player.get());
    assert(!player.remember(nullptr) && !player.get());

    // Serial reuse invalidates metadata even if the old address is readable.
    ++UObjectArray::items[as_value.index].serial;
    assert(!binding.return_value.valid() && !binding.return_value.number(value));
    assert(binding.resolve(&owner,L"/Script/ScaleformUI.GFxObject:GetMemberValue",false)==&getter);
    assert(binding.return_value.valid() && field_lookups>fields);
    ++UObjectArray::items[getter.index].serial;
    const int resolutions=owner.function_lookups;
    assert(binding.resolve(&owner,L"/Script/ScaleformUI.GFxObject:GetMemberValue",false)==&getter);
    assert(owner.function_lookups==resolutions+1);

    // No access is permitted past the native parameter buffer on a bad layout.
    ++UObjectArray::items[getter.index].serial;
    returned.offset=70;
    assert(!binding.resolve(&owner,L"/Script/ScaleformUI.GFxObject:GetMemberValue",false));
    returned.offset=16;
    assert(binding.resolve(&owner,L"/Script/ScaleformUI.GFxObject:GetMemberValue",false)==&getter);
}
