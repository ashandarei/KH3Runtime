#include "kh3ap/core/reflection/owned_function_cache.h"
#include "kh3ap/core/reflection/named_function_cache.h"
#include "kh3ap/core/reflection/receiver_function_cache.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/ui/live_ui_parts_cache.h"
#include <cassert>
#include <cstring>

using namespace RC::Unreal;
using namespace kh3ap;
namespace {
auto add(UObject& object,UClass* type,UObject* outer=nullptr,int serial=1) -> void {
    object.type=type; object.outer=outer; object.index=UObjectArray::GetNumElements();
    UObjectArray::items.push_back({&object,true,serial});
}
}
auto kh3ap::make_uobject_weak(UObject* object) -> FWeakObjectPtr {
    FWeakObjectPtr weak;
    if (!is_uobject_valid_fast(object)) return weak;
    auto& item=UObjectArray::items[object->index];
    if (!item.serial) item.serial=100;
    weak=object; return weak;
}
int main() {
    UClass meta,root,base,derived,other,fn_type;
    UClass::class_type=&meta; UFunction::function_type=&fn_type;
    for (auto* type:{&meta,&root,&base,&derived,&other,&fn_type}) add(*type,&meta);
    base.parent=other.parent=fn_type.parent=&root; derived.parent=&base;
    UObject first,second,third;
    add(first,&derived); add(second,&derived); add(third,&other);
    UFunction a,b; add(a,&fn_type,&base,0); add(b,&fn_type,&other);
    first.functions[L"Read"]=&a; second.functions[L"Read"]=&a; third.functions[L"Read"]=&b;
    OwnedFunctionCache fn{L"Read"};
    assert(fn.resolve(&first)==&a && fn.resolve(&second)==&a);
    assert(first.function_lookups==1 && second.function_lookups==0);
    assert(fn.resolve(&third)==&b); // Concrete owner changes.
    assert(fn.resolve(&first)==&a);
    ++UObjectArray::items[a.index].serial;
    assert(fn.resolve(&first)==&a && first.function_lookups==3);
    ++UObjectArray::items[derived.index].serial;
    assert(fn.resolve(&first)==&a && first.function_lookups==4);
    ++UObjectArray::items[a.index].serial; a.outer=&other;
    assert(!fn.resolve(&first)); // A function from another class is rejected.
    a.outer=&base;
    assert(fn.resolve(&first)==&a);
    UObjectArray::items[a.index].alive=false;
    assert(!fn.resolve(&first));
    UObjectArray::items[a.index].alive=true;

    {
        NamedFunctionCache<3> named;
        UFunction overridden, arriving;
        add(overridden,&fn_type,&derived); add(arriving,&fn_type,&base);
        first.functions[L"Read"]=&overridden; second.functions[L"Read"]=&overridden;
        const auto first_reads=first.function_lookups;
        const auto second_reads=second.function_lookups;
        assert(named.resolve(&first,L"Read")==&overridden);
        assert(named.resolve(&second,L"Read")==&overridden);
        assert(named.resolve(&third,L"Read")==&b);
        assert(named.resolve(&first,L"Read")==&overridden);
        assert(first.function_lookups==first_reads+1 && second.function_lookups==second_reads);
        ++UObjectArray::items[overridden.index].serial;
        assert(named.resolve(&first,L"Read")==&overridden);
        assert(first.function_lookups==first_reads+2);
        ++UObjectArray::items[derived.index].serial;
        assert(named.resolve(&first,L"Read")==&overridden);
        assert(first.function_lookups==first_reads+3);
        assert(!named.resolve(&first,L"Arriving"));
        first.functions[L"Arriving"]=&arriving;
        assert(named.resolve(&first,L"Arriving")==&arriving);
        first.functions[L"Foreign"]=&b;
        assert(!named.resolve(&first,L"Foreign"));
        UObjectArray::items[overridden.index].alive=false;
        assert(!named.resolve(&first,L"Read"));
        assert(!named.resolve(nullptr,L"Read"));
        UObjectArray::items[overridden.index].alive=true;
        first.functions[L"Read"]=&a; second.functions[L"Read"]=&a;
    }

    {
        ReceiverFunctionCache<2> exact;
        UFunction inherited, overridden, arriving;
        add(inherited,&fn_type,&base); add(overridden,&fn_type,&derived);
        add(arriving,&fn_type,&base);
        inherited.object_name=overridden.object_name=L"Read";
        inherited.object_path=L"/Script/Test.Base:Read";
        overridden.object_path=L"/Script/Test.Derived:Read";
        arriving.object_name=L"Arriving"; arriving.object_path=L"/Script/Test.Base:Arriving";
        derived.hierarchy_functions={&overridden,&inherited};
        assert(exact.resolve(&first,inherited.object_path)==&inherited);
        assert(exact.resolve(&first,overridden.object_path)==&overridden);
        const auto scans=derived.function_chain_reads;
        assert(exact.resolve(&second,inherited.object_path)==&inherited);
        assert(derived.function_chain_reads==scans); // Same class, another receiver.
        assert(!exact.resolve(&third,inherited.object_path)); // Different owner chain.
        assert(!exact.resolve(&first,L"/Script/Test.Base:Arriving"));
        derived.hierarchy_functions.push_back(&arriving);
        assert(exact.resolve(&first,arriving.object_path)==&arriving); // Misses can recover.
        assert(exact.resolve(&first,inherited.object_path)==&inherited); // Bounded eviction.
        ++UObjectArray::items[inherited.index].serial;
        const auto invalidated=derived.function_chain_reads;
        assert(exact.resolve(&first,inherited.object_path)==&inherited);
        assert(derived.function_chain_reads==invalidated+1); // Same-address reuse rebinds.
        ++UObjectArray::items[derived.index].serial;
        assert(exact.resolve(&first,inherited.object_path)==&inherited);
        UObjectArray::items[inherited.index].alive=false;
        assert(!exact.resolve(&first,inherited.object_path));
        UObjectArray::items[inherited.index].alive=true;
        assert(!exact.resolve(nullptr,inherited.object_path));
        assert(!exact.resolve(&first,L"Read"));
        assert(!exact.resolve(&first,L"/Script/Test.Base:"));
        derived.parent=&derived;
        assert(!exact.resolve(&first,overridden.object_path)); // Corrupt hierarchy rejected.
        derived.parent=&base;
    }

    FProperty field{{L"ObjectProperty"},8,8}, alternate{{L"ObjectProperty"},16,8};
    first.properties[L"Live"]=&field; second.properties[L"Live"]=&field;
    third.properties[L"Live"]=&alternate;
    {
        ReflectedPropertyCache<2> typed;
        assert(typed.resolve_object(&first,L"Live")==&field);
        const auto classifications=field.type.name_reads;
        for (int i=0;i<50;++i) assert(typed.resolve_object(&second,L"Live")==&field);
        assert(field.type.name_reads==classifications); // Validate immutable type once.
        ++UObjectArray::items[derived.index].serial;
        assert(typed.resolve_object(&first,L"Live")==&field);
        assert(field.type.name_reads==classifications+1);
        FProperty wrong{{L"IntProperty"},8,8};
        first.properties[L"Wrong"]=&wrong;
        assert(!typed.resolve_object(&first,L"Wrong"));
        assert(typed.resolve(&first,L"Wrong")==&wrong); // Untyped callers retain behavior.
        const auto rejected=wrong.type.name_reads;
        assert(!typed.resolve_object(&second,L"Wrong"));
        assert(wrong.type.name_reads==rejected);
        FProperty short_object{{L"ObjectProperty"},8,4};
        first.properties[L"Short"]=&short_object;
        assert(!typed.resolve_object(&first,L"Short"));
        assert(!typed.resolve_object(&first,L"Late"));
        first.properties[L"Late"]=&field;
        assert(typed.resolve_object(&first,L"Late")==&field);
        ++UObjectArray::items[derived.index].serial;
        first.properties[L"Late"]=&wrong;
        assert(!typed.resolve_object(&first,L"Late"));
        assert(!typed.resolve_object(nullptr,L"Live"));
    }
    {
        struct Holder : UObject { UObject* target{}; } holder;
        add(holder,&derived);
        const auto offset=static_cast<int32_t>(reinterpret_cast<std::byte*>(&holder.target)-reinterpret_cast<std::byte*>(&holder));
        FProperty target{{L"ObjectProperty"},offset,sizeof(UObject*)};
        holder.properties[L"Target"]=&target;
        ReflectedPropertyCache<1> pointers;
        assert(!pointers.read_object(&holder,L"Target"));
        holder.target=&first;
        assert(pointers.read_object(&holder,L"Target")==&first);
        holder.target=&second;
        assert(pointers.read_object(&holder,L"Target")==&second);
        assert(holder.property_lookups==1); // The value changes without a metadata lookup.
        UObjectArray::items[second.index].alive=false;
        assert(!pointers.read_object(&holder,L"Target"));
        UObjectArray::items[second.index].alive=true;
        holder.target=nullptr;
        assert(!pointers.read_object(&holder,L"Target"));
        assert(!pointers.read_object(nullptr,L"Target"));
    }
    first.property_lookups=second.property_lookups=third.property_lookups=0;
    ReflectedPropertyCache<2> properties;
    assert(properties.resolve(&first,L"Live")==&field);
    assert(properties.resolve(&second,L"Live")==&field && second.property_lookups==0);
    assert(properties.resolve(&third,L"Live")==&alternate);
    ++UObjectArray::items[derived.index].serial;
    first.properties[L"Live"]=&alternate;
    assert(properties.resolve(&first,L"Live")==&alternate && first.property_lookups==2);
    assert(!properties.resolve(&first,L"Arriving")); // A miss can recover.
    first.properties[L"Arriving"]=&field;
    assert(properties.resolve(&first,L"Arriving")==&field);
    assert(!properties.resolve(nullptr,L"Live"));
    // Bounded eviction must rebind instead of reusing an unrelated property.
    first.properties[L"Third"]=&field;
    assert(properties.resolve(&first,L"Third")==&field);
    assert(properties.resolve(&third,L"Live")==&alternate);
    ++UObjectArray::items[derived.index].serial;
    field.offset=-1;
    assert(!properties.resolve(&first,L"Third"));
    field.offset=8;
    derived.parent=&derived;
    assert(!properties.resolve(&first,L"Third"));

    UClass hud_type,nav_type;
    add(hud_type,&meta); add(nav_type,&meta);
    hud_type.object_name=L"TresUIP_HudRight"; nav_type.object_name=L"TresUIP_NaviMapGra";
    UObject hud,nav,replacement,manager,next_manager;
    add(hud,&hud_type); add(nav,&nav_type); add(replacement,&nav_type);
    add(manager,&root); add(next_manager,&root);
    UObject* array[]{&hud,&first,&nav};
    LiveUiPartsCache slots;
    assert(slots.resolve(&manager,array,3)==std::make_pair(&hud,&nav));
    const auto names=hud_type.name_reads+nav_type.name_reads;
    for (int i=0;i<50;++i) assert(slots.resolve(&manager,array,3)==std::make_pair(&hud,&nav));
    assert(hud_type.name_reads+nav_type.name_reads==names);
    std::swap(array[0],array[2]);
    assert(slots.resolve(&manager,array,3)==std::make_pair(&hud,&nav));
    array[0]=&replacement;
    assert(slots.resolve(&manager,array,3)==std::make_pair(&hud,&replacement));
    ++UObjectArray::items[replacement.index].serial;
    assert(slots.resolve(&manager,array,3)==std::make_pair(&hud,&replacement));
    assert(slots.resolve(&next_manager,array,3)==std::make_pair(&hud,&replacement));
    array[0]=nullptr;
    assert(slots.resolve(&next_manager,array,3)==std::make_pair(&hud,static_cast<UObject*>(nullptr)));
    array[0]=&nav;
    assert(slots.resolve(&next_manager,array,3)==std::make_pair(&hud,&nav));
    assert(!slots.resolve(&next_manager,array,2).first);
    assert(!slots.resolve(&next_manager,array,129).first);
    // A new last matching entry must win even when the old slots remain valid.
    UObject* expanded[]{&hud,&first,&nav,&replacement};
    assert(slots.resolve(&next_manager,expanded,4)==std::make_pair(&hud,&replacement));
    // A same-sized array mutation outside cached slots must also rediscover.
    expanded[3]=&first;
    assert(slots.resolve(&next_manager,expanded,4)==std::make_pair(&hud,&nav));
    expanded[3]=&replacement;
    assert(slots.resolve(&next_manager,expanded,4)==std::make_pair(&hud,&replacement));
}
