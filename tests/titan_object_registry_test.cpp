#include "kh3ap/features/enemies/titan_object_registry.h"
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
    namespace objects=kh3ap::titan_objects;
    UClass type;type.name=FName(L"StaticMeshActor",0);
    UObject wall,ignored;wall.name=FName(L"he_05_invisiblewall_01",0);
    ignored.name=FName(L"some_other_mesh",0);add(wall,type);add(ignored,type);
    objects::watch_pawn_class(0,L"/Game/Blueprints/Enemy/e_ex353/e_ex353_Pawn.e_ex353_Pawn_C");
    objects::initialize();objects::initialize();
    auto snapshot=objects::snapshot();assert(snapshot.objects==std::vector<UObject*>{&wall});
    assert(snapshot.bootstrap_scans==1&&UObjectGlobals::enumerations==1);
    const auto revision=objects::generation();objects::constructed(&ignored);assert(objects::generation()==revision);
    UObject late;late.name=FName(L"e_he904_Pawn1045",0);add(late,type,RF_NeedPostLoad);
    std::thread loader([&]{objects::constructed(&late);});loader.join();
    snapshot=objects::snapshot();assert(snapshot.unfinished&&snapshot.objects.size()==1);
    late.flags=0;assert(objects::snapshot().objects.size()==2);
    UObject weak_pending;weak_pending.name=FName(L"he_05_invisiblewall_02",0);add(weak_pending,type);
    weak_pending.fail_weak=true;objects::constructed(&weak_pending);assert(objects::snapshot().unfinished);
    weak_pending.fail_weak=false;assert(objects::snapshot().objects.size()==3);
    remove(weak_pending);
    UObject recycled;recycled.name=FName(L"SM_he_Ter_ZeusRockBattleA",0);add(recycled,type);
    objects::constructed(&recycled);remove(recycled);UObjectArray::items[recycled.index].object=&recycled;
    recycled.name=FName(L"unrelated",0);objects::constructed(&recycled);assert(objects::snapshot().objects.size()==2);
    remove(wall);assert(objects::snapshot().objects.size()==1);
    UClass boss_type;boss_type.name=FName(L"e_ex353_Pawn_C",0);
    UObject boss;boss.name=FName(L"e_ex353_Pawn_C_0",0);add(boss,boss_type);
    objects::constructed(&boss);snapshot=objects::snapshot();
    assert(snapshot.objects.size()==1&&snapshot.pawns==std::vector<UObject*>{&boss});
    remove(boss);assert(objects::snapshot().pawns.empty());
    for(int i=0;i<10000;++i)assert(objects::snapshot().bootstrap_scans==1);
    assert(UObjectGlobals::enumerations==1);
    std::cout<<"Titan registry: bootstrap, exact names, streamed PostLoad, weak retries, GC/reuse passed\n";
}
