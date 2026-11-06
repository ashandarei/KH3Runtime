#include "kh3ap/features/grants/battle_level/battle_level_functions.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/Runtime.hpp>
#include <cassert>

using namespace RC::Unreal;
using namespace kh3ap;

namespace
{
    UObject* loaded_library{};
    UClass* loaded_class{};
    int library_resolutions{}, class_resolutions{};
    auto register_object(UObject& object, UClass* type, UObject* outer = nullptr) -> void
    {
        object.type = type;
        object.outer = outer;
        object.index = UObjectArray::GetNumElements();
        UObjectArray::items.push_back({&object});
    }
}

auto kh3ap::find_loaded_runtime_asset(std::wstring_view path) -> UObject*
{
    assert(path == L"/Script/TresGame.Default__TresGameBlueprintLibrary");
    ++library_resolutions;
    return loaded_library;
}
auto kh3ap::find_loaded_runtime_asset_class(std::wstring_view path) -> UClass*
{
    assert(path == L"/Script/TresGame.TresGameBlueprintLibrary");
    ++class_resolutions;
    return loaded_class;
}

int main()
{
    UClass meta, root, library_type, function_type;
    UClass::class_type = &meta;
    UFunction::function_type = &function_type;
    for (auto* type : {&meta, &root, &library_type, &function_type}) register_object(*type, &meta);
    library_type.parent = function_type.parent = &root;
    loaded_class = &library_type;
    UObject library;
    register_object(library, &library_type);
    loaded_library = &library;
    UFunction getter, setter, new_getter;
    for (auto* f : {&getter, &setter, &new_getter}) register_object(*f, &function_type, &library_type);
    library.functions = {{L"BP_TresGetBattleLevel", &getter}, {L"BP_TresSetBattleLevel", &setter}};
    RuntimeBattleLevelFunctions cache;
    cache.prepare();
    assert(library_resolutions == 1 && class_resolutions == 1 && library.function_lookups == 2);
    for (int i = 0; i < 20; ++i)
    {
        const auto get = cache.resolve(true), set = cache.resolve(false);
        assert(get.library == &library && get.function == &getter && set.function == &setter);
    }
    assert(library_resolutions == 1 && library.function_lookups == 2);

    // GC/slot reuse must resolve the replacement instead of using a stale
    // cached function, even when the old storage address is still readable.
    ++UObjectArray::items[getter.index].serial;
    library.functions[L"BP_TresGetBattleLevel"] = &new_getter;
    assert(cache.resolve(true).function == &new_getter);
    assert(library.function_lookups == 3);

    // Replacement of the owner invalidates both function caches.
    ++UObjectArray::items[library.index].serial;
    assert(cache.resolve(false).function == &setter);
    assert(library_resolutions == 2 && library.function_lookups == 4);
    assert(cache.resolve(true).function == &new_getter);
    assert(library.function_lookups == 5);

    // Missing/mistyped assets and wrong owners fail closed and can recover.
    UObjectArray::items[library.index].alive = false;
    loaded_library = nullptr;
    assert(!cache.resolve(false).function);
    ++UObjectArray::items[library.index].serial;
    UObjectArray::items[library.index].alive = true;
    UObject wrong;
    register_object(wrong, &root);
    loaded_library = &wrong;
    assert(!cache.resolve(true).function);
    loaded_library = &library;
    getter.outer = &root;
    library.functions[L"BP_TresGetBattleLevel"] = &getter;
    assert(!cache.resolve(true).function);
    getter.outer = &library_type;
    assert(cache.resolve(true).function == &getter);
    getter.type = &root;
    assert(!cache.resolve(true).function);
    getter.type = &function_type;
    assert(cache.resolve(true).function == &getter);

    // Class ancestry must be checked before iterating its function chain.
    library_type.parent = reinterpret_cast<UStruct*>(&wrong);
    const int lookups = library.function_lookups;
    assert(!cache.resolve(true).function && library.function_lookups == lookups);
    library_type.parent = &root;
    assert(cache.resolve(true).function == &getter);
}
