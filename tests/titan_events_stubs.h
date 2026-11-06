#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include <stdexcept>
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/features/enemies/titan_object_registry.h"
#define STR(s) L##s
namespace RC {
enum class LogLevel { Normal,Warning };
struct Output {template<LogLevel,class... A> static void send(A&&...) {}};
namespace Unreal {
struct UClass { bool alive{true};std::wstring path;std::wstring GetPathName(){return path;} };
class UObject {public:
    bool alive{true},collision{true},hidden{};int serial{1},writes{};
    std::wstring path;UClass* type{};UObject* outer{};UObject* entity_actor{};
    std::wstring GetPathName(){return path;}
    UClass* GetClassPrivate(){return type;}
    UObject* GetOuterPrivate(){return outer;}
};
struct FWeakObjectPtr {
    UObject* object{};int serial{};
    void operator=(UObject* o){object=o;serial=o?o->serial:0;}
    UObject* Get()const{return object&&object->alive&&object->serial==serial?object:nullptr;}
};
}}
namespace kh3ap {
template<class T> bool is_uobject_valid_fast(T* o){return o&&o->alive;}
inline RC::Unreal::FWeakObjectPtr make_uobject_weak(RC::Unreal::UObject* o){RC::Unreal::FWeakObjectPtr r;r=o;return r;}
inline RC::Unreal::UObject* try_read_reflected_object_property_value(RC::Unreal::UObject* o,const wchar_t*){return o->entity_actor;}
namespace fake_titan {
inline int calls{},snapshots{};
inline uint64_t revision{};
inline titan_objects::Snapshot objects;
struct Clock {
    static inline std::chrono::steady_clock::time_point time{};
    static auto now(){return time;}
};
}
namespace titan_objects {
inline void watch_pawn_class(size_t,std::wstring_view){}
inline void initialize(){}
inline uint64_t generation(){return fake_titan::revision;}
inline Snapshot snapshot(){++fake_titan::snapshots;return fake_titan::objects;}
}
}
