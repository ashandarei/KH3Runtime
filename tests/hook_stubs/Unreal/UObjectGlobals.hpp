#pragma once
#include "Hooks.hpp"
#include <Unreal/UFunction.hpp>
#include <functional>
#include <string>
#include <utility>
namespace RC::Unreal::UObjectGlobals
{
    using Callback = std::function<void(UnrealScriptFunctionCallableContext&, void*)>;
    auto find_function(const std::wstring&) -> UFunction*;
    template <typename T>
    auto StaticFindObject(UObject*, UObject*, const std::wstring& path) -> T { return static_cast<T>(find_function(path)); }
    auto RegisterHook(UFunction*, Callback, Callback, void*) -> std::pair<int, int>;
    auto UnregisterHook(UFunction*, std::pair<int, int>) -> void;
}
