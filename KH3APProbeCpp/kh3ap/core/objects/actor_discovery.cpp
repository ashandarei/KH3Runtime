#include "kh3ap/core/objects/actor_discovery.h"

#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>

#include <cstdint>
#include <string_view>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_actor_array_parameter_bytes = 256;
    }

    using namespace RC::Unreal;

    auto find_live_actors_of_class(UObject* world_context, UClass* actor_class, size_t limit)
            -> std::vector<AActor*>
    {
        if (!is_uobject_a_fast(world_context, AActor::StaticClass()) || !limit ||
            !is_uclass_child_of_fast(actor_class, AActor::StaticClass())) return {};

        // UE4SS's GameplayStatics wrapper keeps raw Self/UFunction pointers.
        // Its IsValid only checks for null, so do not reuse it after travel/GC.
        auto* owner = find_loaded_runtime_asset(L"/Script/Engine.Default__GameplayStatics");
        auto* owner_class = find_loaded_runtime_asset_class(L"/Script/Engine.GameplayStatics");
        if (!is_uobject_a_fast(owner, owner_class)) return {};
        auto* function = owner->GetFunctionByNameInChain(L"GetAllActorsOfClass");
        if (!is_uobject_a_fast(function, UFunction::StaticClass())) return {};

        const auto size = function->GetParmsSize();
        if (size <= 0 || size > max_actor_array_parameter_bytes) return {};
        auto* context = find_function_param_by_name(function, L"WorldContextObject");
        auto* requested_class = find_function_param_by_name(function, L"ActorClass");
        auto* result = find_function_param_by_name(function, L"OutActors");
        struct Array { UObject** data; int32_t count, capacity; };
        const auto valid_param = [size](FProperty* property, std::wstring_view type, size_t bytes) {
            return property && property->HasAnyPropertyFlags(CPF_Parm) &&
                    property->GetClass().GetName() == type &&
                    property->GetElementSize() == bytes &&
                    property->GetOffset_Internal() >= 0 &&
                    property->GetOffset_Internal() <= size - static_cast<int32_t>(bytes);
        };
        if (!valid_param(context, L"ObjectProperty", sizeof(UObject*)) ||
            !valid_param(requested_class, L"ClassProperty", sizeof(UClass*)) ||
            !valid_param(result, L"ArrayProperty", sizeof(Array)) ||
            !result->HasAnyPropertyFlags(CPF_OutParm)) return {};
        auto* inner = static_cast<FArrayProperty*>(result)->GetInner();
        if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
            inner->GetElementSize() != sizeof(UObject*)) return {};

        std::vector<std::byte> params(size);
        function->InitializeStruct(params.data());
        std::vector<AActor*> actors;
        try
        {
            *context->ContainerPtrToValuePtr<UObject*>(params.data()) = world_context;
            *requested_class->ContainerPtrToValuePtr<UClass*>(params.data()) = actor_class;
            owner->ProcessEvent(function, params.data());
            const auto* returned = result->ContainerPtrToValuePtr<Array>(params.data());
            if (returned->data && returned->count > 0 &&
                static_cast<size_t>(returned->count) <= limit && returned->capacity >= returned->count)
            {
                for (int32_t index = 0; index < returned->count; ++index)
                {
                    auto* object = returned->data[index];
                    if (is_uobject_a_fast(object, actor_class))
                        actors.push_back(static_cast<AActor*>(object));
                }
            }
        }
        catch (...)
        {
            actors.clear();
        }
        function->DestroyStruct(params.data());
        return actors;
    }
}
