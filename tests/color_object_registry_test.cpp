#include "kh3ap/features/colors/color_object_registry.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObjectGlobals.hpp>
#include <cassert>
#include <thread>
#include <iostream>

using namespace RC::Unreal;
namespace kh3ap {
    bool is_uobject_valid_fast(const UObject* object) noexcept {
        return object && object->index >= 0 && size_t(object->index) < UObjectArray::items.size() &&
            UObjectArray::items[object->index].object == object;
    }
    FWeakObjectPtr make_uobject_weak(UObject* object) {
        FWeakObjectPtr result;
        if (is_uobject_valid_fast(object) && !object->fail_weak) {
            auto& slot=UObjectArray::items[object->index];
            if (!slot.serial) slot.serial=1;
            result.index=object->index;result.serial=slot.serial;
        }
        return result;
    }
}
void add(UObject& object, UClass& type, int flags=0) {
    object.type=&type;object.flags=flags;object.index=int(UObjectArray::items.size());
    UObjectArray::items.push_back({&object,0});
}
void remove(UObject& object) {
    for(auto* listener:UObjectArray::listeners)listener->NotifyUObjectDeleted(&object,object.index);
    auto& slot=UObjectArray::items[object.index];slot.object=nullptr;++slot.serial;
}
int main() {
    namespace objects=kh3ap::color_objects;
    UClass pawn,derived,other;
    pawn.name=FName(L"Pawn",0);derived.name=FName(L"Ralph_Pawn_C",0);derived.parent=&pawn;
    other.name=FName(L"Unrelated",0);
    UObject a,cd,ignored;add(a,derived);add(cd,derived,RF_ClassDefaultObject);add(ignored,other);
    UClass weapon_base, keyblade_type;
    weapon_base.name = FName(L"TresWeaponBase", 0);
    weapon_base.parent = &pawn;
    keyblade_type.name = FName(L"w_so120_Wep_C", 0);
    keyblade_type.parent = &weapon_base;
    UObject initial_weapon;
    add(initial_weapon, keyblade_type);
    UClass spectator_type;
    spectator_type.name = FName(L"SpectatorPawn", 0);
    spectator_type.parent = &pawn;
    UObject initial_spectator;
    add(initial_spectator, spectator_type);
    objects::initialize();objects::initialize();
    auto snapshot=objects::snapshot();
    assert(snapshot.bootstrap_scans==1&&UObjectGlobals::enumerations==1);
    assert(snapshot.pawns==std::vector<UObject*>{&a});
    const auto initial=objects::generation();
    objects::constructed(&a);objects::constructed(&ignored);assert(objects::generation()==initial);
    // Weapon/spectator creation and destruction must not wake either consumer.
    // Cover bootstrap, loader-thread spawn, unfinished PostLoad and replacement.
    const auto before_weapon_gummi_generation = objects::gummi_generation();
    for (int swap = 0; swap < 4; ++swap) {
        UObject weapon;
        add(weapon, keyblade_type, RF_NeedPostLoad);
        UObject spectator;
        add(spectator, spectator_type, RF_NeedPostLoad);
        std::thread weapon_loader([&] {
            objects::constructed(&weapon);
            objects::constructed(&spectator);
        });
        weapon_loader.join();
        assert(objects::generation() == initial);
        assert(objects::gummi_generation() == before_weapon_gummi_generation);
        snapshot = objects::snapshot();
        assert(!snapshot.unfinished && snapshot.pawns == std::vector<UObject*>{&a});
        weapon.flags = 0;
        spectator.flags = 0;
        objects::constructed(&weapon);
        objects::constructed(&spectator);
        remove(weapon);
        remove(spectator);
        assert(objects::generation() == initial);
        assert(objects::gummi_generation() == before_weapon_gummi_generation);
    }
    remove(initial_weapon);
    remove(initial_spectator);
    assert(objects::generation() == initial);
    UObject stream;add(stream,derived,RF_NeedPostLoad);
    std::thread loader([&]{objects::constructed(&stream);});loader.join();
    assert(objects::generation()>initial);
    snapshot=objects::snapshot();assert(snapshot.unfinished&&snapshot.pawns.size()==1);
    stream.flags=0;snapshot=objects::snapshot();assert(!snapshot.unfinished&&snapshot.pawns.size()==2);
    UObject pending;add(pending,derived);pending.fail_weak=true;objects::constructed(&pending);
    assert(objects::snapshot().unfinished);pending.fail_weak=false;
    assert(!objects::snapshot().unfinished&&objects::snapshot().pawns.size()==3);
    UObject reused;add(reused,derived);objects::constructed(&reused);remove(reused);
    reused.type=&other;UObjectArray::items[reused.index].object=&reused;objects::constructed(&reused);
    assert(objects::snapshot().pawns.size()==3);
    remove(a);UObjectArray::items[a.index].object=&a;objects::constructed(&a);
    snapshot=objects::snapshot();assert(snapshot.pawns.size()==3&&snapshot.pawns.front()==&a);
    remove(stream);assert(objects::snapshot().pawns.size()==2);
    UClass ship_type,editor_type,table_type,world_type;
    ship_type.name=FName(L"TresCreateGummiShipActor",0);ship_type.parent=&pawn;
    editor_type.name=FName(L"TresGummiEditorManager",0);table_type.name=FName(L"DataTable",0);
    world_type.name=FName(L"World",0);
    UObject ship,editor,table,world;
    add(ship,ship_type);add(editor,editor_type);add(table,table_type,RF_NeedPostLoad);add(world,world_type);
    const auto pawn_generation=objects::generation(),gummi_generation=objects::gummi_generation();
    for(auto* o:{&ship,&editor,&table,&world}) objects::constructed(o);
    assert(objects::generation()==pawn_generation&&objects::gummi_generation()>gummi_generation);
    snapshot=objects::snapshot();assert(!snapshot.unfinished&&snapshot.pawns.size()==2);
    snapshot=objects::snapshot(true);
    assert(snapshot.unfinished&&snapshot.ships==std::vector<UObject*>{&ship}&&snapshot.editors.size()==1&&snapshot.tables.empty()&&snapshot.worlds.size()==1);
    table.flags=0;snapshot=objects::snapshot(true);assert(!snapshot.unfinished&&snapshot.tables.size()==1);
    remove(ship);assert(objects::snapshot(true).ships.empty());
    for(int i=0;i<10000;++i)assert(objects::snapshot().bootstrap_scans==1);
    assert(UObjectGlobals::enumerations==1);
    std::cout<<"color registry: initial enumeration, late spawn, PostLoad, weak retries, GC/address reuse, 10000 snapshots passed\n";
}
