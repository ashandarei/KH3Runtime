#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <functional>
namespace RC {
enum class LoopAction { Continue };
namespace Unreal {
using int32=int32_t;
enum EObjectFlags { RF_ClassDefaultObject=1, RF_ArchetypeObject=2, RF_NeedLoad=4, RF_NeedPostLoad=8, RF_NeedPostLoadSubobjects=16 };
enum { FNAME_Add };
struct FName {
    std::wstring value;
    FName()=default;
    FName(const wchar_t* name,int):value(name){}
    bool Equals(const FName& other) const { return value==other.value; }
};
class UObject; using UObjectBase=UObject; class UClass;
struct FUObjectItem { UObject* object{}; int serial{1}; UObject* GetUObject() {return object;} };
struct FUObjectDeleteListener {
    virtual ~FUObjectDeleteListener()=default;
    virtual void NotifyUObjectDeleted(const UObjectBase*,int32)=0;
    virtual void OnUObjectArrayShutdown()=0;
};
struct UObjectArray {
    static inline std::deque<FUObjectItem> items;
    static inline std::vector<FUObjectDeleteListener*> listeners;
    static void AddUObjectDeleteListener(FUObjectDeleteListener* l){listeners.push_back(l);}
    static void RemoveUObjectDeleteListener(FUObjectDeleteListener* l){std::erase(listeners,l);}
};
class UObject {
public:
    UClass* type{}; int index{-1},flags{}; FName name;
    bool fail_weak{};
    bool HasAnyFlags(EObjectFlags f) const {return (flags&f)!=0;}
    UClass* GetClassPrivate() const {return type;}
    const FName& GetNamePrivate() const {return name;}
    FUObjectItem* GetObjectItem() const {return &UObjectArray::items[index];}
    int GetInternalIndex() const {return index;}
};
class UClass:public UObject {public: UClass* parent{}; UClass* GetSuperStruct(){return parent;}};
struct FWeakObjectPtr {
    int index{-1},serial{};
    UObject* Get() const {
        if(index<0||size_t(index)>=UObjectArray::items.size())return nullptr;
        const auto& slot=UObjectArray::items[index];
        return slot.serial==serial&&serial>0 ? slot.object:nullptr;
    }
};
struct UObjectGlobals {
    static inline size_t enumerations{};
    template<class F> static void ForEachUObject(F fn) {
        ++enumerations;
        for(size_t i=0;i<UObjectArray::items.size();++i)
            if(auto* o=UObjectArray::items[i].object)fn(o,int(i),0);
    }
};
}}
