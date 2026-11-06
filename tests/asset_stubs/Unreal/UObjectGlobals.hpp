#pragma once
#include <Unreal/Runtime.hpp>
namespace RC::Unreal {
namespace UObjectGlobals {
auto find(const wchar_t*) -> UObject*;
template<class T> auto StaticFindObject(void*,void*,const wchar_t* path) -> T { return static_cast<T>(find(path)); }
}
}
