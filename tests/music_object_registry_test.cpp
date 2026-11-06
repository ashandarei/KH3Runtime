#include "kh3ap/features/music/music_object_registry.h"
#include "kh3ap/features/music/events/music_update_events.h"
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
    namespace objects=kh3ap::music_objects;
    UClass settings,asset,manager,derived,other;
    settings.name=FName(L"TresWorldSettings",0);asset.name=FName(L"TresWorldCodeLoadAsset",0);
    manager.name=FName(L"SQEXSEADBGMManager",0);derived.name=FName(L"TresWorldLoadHE_C",0);derived.parent=&asset;
    other.name=FName(L"Unrelated",0);
    UObject a,b,c,cd,ar,ignored;
    add(a,settings);add(b,derived);add(c,manager);add(cd,settings,RF_ClassDefaultObject);
    add(ar,asset,RF_ArchetypeObject);add(ignored,other);
    objects::initialize();objects::initialize();
    auto snapshot=objects::snapshot();
    assert(snapshot.bootstrap_scans==1&&UObjectGlobals::enumerations==1);
    assert(snapshot.settings==std::vector<UObject*>{&a});
    assert(snapshot.load_assets==std::vector<UObject*>{&b});
    assert(snapshot.managers==std::vector<UObject*>{&c});
    kh3ap::take_music_updates();
    objects::constructed(&a);objects::constructed(&ignored);
    assert(kh3ap::take_music_updates()==0); // duplicates and unrelated construction stay quiet
    UObject stream;
    add(stream,settings,RF_NeedPostLoad);
    std::thread loader([&]{objects::constructed(&stream);});loader.join();
    assert(kh3ap::take_music_updates()==uint32_t(kh3ap::MusicUpdate::Objects));
    snapshot=objects::snapshot();assert(snapshot.unfinished&&snapshot.settings.size()==1);
    stream.flags=0;
    snapshot=objects::snapshot();assert(!snapshot.unfinished&&snapshot.settings.size()==2);
    // Native weak serial allocation failures retain pending ownership for retry.
    UObject pending;add(pending,derived);pending.fail_weak=true;objects::constructed(&pending);
    assert(objects::snapshot().unfinished);pending.fail_weak=false;
    assert(!objects::snapshot().unfinished&&objects::snapshot().load_assets.size()==2);
    // Delete before drain, then reuse the same address and array slot as an unrelated object.
    UObject reused;add(reused,settings);objects::constructed(&reused);remove(reused);
    reused.type=&other;UObjectArray::items[reused.index].object=&reused;objects::constructed(&reused);
    assert(objects::snapshot().settings.size()==2);
    // Tracked GC, followed by same-address/same-class replacement, gets a new weak identity.
    remove(a);UObjectArray::items[a.index].object=&a;objects::constructed(&a);
    snapshot=objects::snapshot();assert(snapshot.settings.size()==2&&snapshot.settings.front()==&a);
    remove(stream);assert(objects::snapshot().settings.size()==1);
    // Many transitions and snapshots cannot trigger another global enumeration.
    for(int i=0;i<10000;++i)assert(objects::snapshot().bootstrap_scans==1);
    assert(UObjectGlobals::enumerations==1);
    std::cout<<"music registry: bootstrap, streaming, GC, slot reuse, weak retry and 10000 snapshots passed\n";
}
