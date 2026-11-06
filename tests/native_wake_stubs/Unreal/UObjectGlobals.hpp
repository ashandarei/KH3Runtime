#pragma once
#include "UFunction.hpp"
#include <functional>
#include <string>
#include <utility>

namespace RC::Unreal::UObjectGlobals
{
    struct Context {};
    using Callback = std::function<void(Context&, void*)>;
    auto find_function() -> UFunction*;
    template <typename T> auto StaticFindObject(UObject*, UObject*, const wchar_t*) -> T
    {
        return static_cast<T>(find_function());
    }
    auto RegisterHook(UFunction*, Callback, Callback, void*) -> std::pair<int, int>;
    auto UnregisterHook(const std::wstring&, std::pair<int, int>) -> void;
}
