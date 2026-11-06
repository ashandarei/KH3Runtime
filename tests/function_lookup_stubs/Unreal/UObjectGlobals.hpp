#pragma once
#include "UFunction.hpp"
namespace RC::Unreal::UObjectGlobals
{
    inline UFunction* current{};
    inline int resolves{};
    template <class T> auto StaticFindObject(void*, void*, const wchar_t*) -> T
    {
        ++resolves;
        return current;
    }
} // namespace RC::Unreal::UObjectGlobals
