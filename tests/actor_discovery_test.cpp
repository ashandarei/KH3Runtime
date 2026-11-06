#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/Runtime.hpp>
#include <cassert>
#include <stdexcept>

using namespace RC::Unreal;
using namespace kh3ap;

namespace
{
    UObject* current_owner{};
    UClass* current_owner_class{};
    UClass* expected_actor_class{};
    UObject* expected_context{};
    std::vector<UObject*> returned;
    int32_t returned_count{}, returned_capacity{};
    bool fail_call{};

    void register_object(UObject& object, UClass* type)
    {
        object.type = type;
        object.index = UObjectArray::GetNumElements();
        UObjectArray::items.push_back({&object});
    }
}

auto kh3ap::find_loaded_runtime_asset(std::wstring_view path) -> UObject*
{
    assert(path == L"/Script/Engine.Default__GameplayStatics");
    return current_owner;
}
auto kh3ap::find_loaded_runtime_asset_class(std::wstring_view path) -> UClass*
{
    assert(path == L"/Script/Engine.GameplayStatics");
    return current_owner_class;
}
auto kh3ap::find_function_param_by_name(UFunction* function, std::wstring_view name) -> FProperty*
{
    if (name == L"WorldContextObject") return function->context;
    if (name == L"ActorClass") return function->requested_class;
    if (name == L"OutActors") return function->result;
    return nullptr;
}
void UObject::ProcessEvent(UFunction* selected, void* params)
{
    assert(selected == function);
    assert(*selected->context->ContainerPtrToValuePtr<UObject*>(params) == expected_context);
    assert(*selected->requested_class->ContainerPtrToValuePtr<UClass*>(params) == expected_actor_class);
    ++calls;
    if (fail_call) throw std::runtime_error("engine boundary failure");
    struct Array { UObject** data; int32_t count, capacity; };
    *selected->result->ContainerPtrToValuePtr<Array>(params) =
            {returned.data(), returned_count, returned_capacity};
}

int main()
{
    UClass metaclass, root, actor_type, detector_type, function_type, owner_type;
    UClass::class_type = &metaclass;
    AActor::actor_type = &actor_type;
    UFunction::function_type = &function_type;
    for (auto* type : {&metaclass, &root, &actor_type, &detector_type, &function_type, &owner_type})
        register_object(*type, &metaclass);
    actor_type.parent = &root;
    detector_type.parent = &actor_type;
    function_type.parent = owner_type.parent = &root;
    assert(is_uclass_child_of_fast(&detector_type, &actor_type));
    assert(!is_uclass_child_of_fast(&root, &actor_type));
    assert(!is_uclass_child_of_fast(nullptr, &actor_type));

    // A stale cache can pass the slot check while the pointer no longer
    // identifying a class. It must never reach native enumeration.
    UObject reused;
    register_object(reused, &root);
    assert(is_uobject_valid_fast(&reused));
    auto* wrong_type = reinterpret_cast<UClass*>(&reused);
    assert(!is_uclass_child_of_fast(wrong_type, &actor_type));
    detector_type.parent = reinterpret_cast<UStruct*>(&reused);
    assert(!is_uclass_child_of_fast(&detector_type, &actor_type));
    detector_type.parent = &detector_type;
    assert(!is_uclass_child_of_fast(&detector_type, &actor_type));
    detector_type.parent = &actor_type;
    // Validate ancestors beyond a matching base too.
    root.parent = &actor_type;
    assert(!is_uclass_child_of_fast(&detector_type, &actor_type));
    root.parent = nullptr;
#if defined(_MSC_VER)
    detector_type.parent = reinterpret_cast<UStruct*>(0xffffffff00000000ULL);
    assert(!is_uclass_child_of_fast(&detector_type, &actor_type));
    detector_type.parent = &actor_type;
#endif

    UObject owner;
    AActor context, first, second;
    UFunction function, replacement;
    register_object(owner, &owner_type);
    register_object(context, &actor_type);
    register_object(first, &detector_type);
    register_object(second, &detector_type);
    register_object(function, &function_type);
    register_object(replacement, &function_type);
    FProperty context_param{{L"ObjectProperty"}, 0, 8};
    FProperty class_param{{L"ClassProperty"}, 8, 8};
    FProperty inner{{L"ObjectProperty"}, 0, 8};
    FArrayProperty out;
    out.type.name = L"ArrayProperty";
    out.offset = 16; out.size = 16; out.flags = CPF_Parm | CPF_OutParm; out.inner = &inner;
    function.context = replacement.context = &context_param;
    function.requested_class = replacement.requested_class = &class_param;
    function.result = replacement.result = &out;
    owner.function = &function;
    current_owner = &owner; current_owner_class = &owner_type;
    expected_context = &context; expected_actor_class = &detector_type;
    returned = {&first, &reused, &second};
    returned_count = returned_capacity = 3;
    auto discover = [&] { return find_live_actors_of_class(&context, &detector_type, 256); };
    assert((discover() == std::vector<AActor*>{&first, &second}));
    assert(function.initialized == 1 && function.destroyed == 1 && owner.calls == 1);
    assert(find_live_actors_of_class(&context, wrong_type, 256).empty());
    assert(owner.calls == 1);

    // A subsequent scan must resolve the replacement function on its live
    // owner, even though the old function pointer is still non-null.
    UObjectArray::items[function.index].alive = false;
    owner.function = &replacement;
    assert(discover().size() == 2);
    assert(function.initialized == 1 && replacement.initialized == 1 && replacement.destroyed == 1);
    owner.function = reinterpret_cast<UFunction*>(&reused);
    assert(discover().empty());
    assert(owner.calls == 2);
    owner.function = &replacement;

    UObjectArray::items[first.index].alive = false;
    assert((discover() == std::vector<AActor*>{&second}));
    returned_count = 257;
    assert(discover().empty());
    returned_count = 3; returned_capacity = 2;
    assert(discover().empty());
    returned_capacity = 3;
    const auto before = owner.calls;
    out.offset = 24;
    assert(discover().empty());
    out.offset = 16; out.flags = CPF_Parm;
    assert(discover().empty());
    out.flags = CPF_Parm | CPF_OutParm; class_param.type.name = L"ObjectProperty";
    assert(discover().empty());
    class_param.type.name = L"ClassProperty";
    assert(owner.calls == before);
    fail_call = true;
    assert(discover().empty());
    assert(replacement.initialized == replacement.destroyed);
    fail_call = false;
    // Reusing a slot for a different object invalidates its former occupant.
    UObjectArray::items[detector_type.index].object = &reused;
    assert(discover().empty());
}
