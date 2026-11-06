#include "kh3ap/features/treasure/music_chest/music_chest.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace kh3ap::music_chest
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto max_music_chest_parameter_bytes = 4096;
    }
    Call::Call(UObject* o, const wchar_t* name) : obj(o)
    {
        if (!is_uobject_valid_fast(o))
        {
            throw std::runtime_error("invalid_mom_call_object");
        }
        fn = o->GetFunctionByNameInChain(name);
        if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() > max_music_chest_parameter_bytes)
        {
            throw std::runtime_error("missing_mom_function");
        }
        data.resize(std::max<int>(1, fn->GetParmsSize()));
    }

    FProperty* Call::field(const wchar_t* name)
    {
        auto* p = find_function_param_by_name(fn, name);
        if (!p || p->GetOffset_Internal() < 0 ||
            p->GetOffset_Internal() + p->GetElementSize() > fn->GetParmsSize())
        {
            throw std::runtime_error("mom_parameter_layout");
        }
        return p;
    }

    Call::~Call()
    {
        // SlotName owns an FString allocated by the reflected parameter writer.
        auto* p = find_function_param_by_name(fn, L"SlotName");
        if (p && p->GetClass().GetName() == L"StrProperty" && p->GetOffset_Internal() >= 0 &&
            p->GetOffset_Internal() + p->GetElementSize() <= fn->GetParmsSize())
        {
            destroy_property_value_dynamic(p, p->ContainerPtrToValuePtr<void>(data.data()));
        }
    }

    void Call::run()
    {
        obj->ProcessEvent(fn, data.data());
    }

    void Call::boolean(const wchar_t* name, bool v)
    {
        if (!write_bool_function_param(field(name), data.data(), v))
        {
            throw std::runtime_error("mom_bool_layout");
        }
    }

    int64_t unix_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    Vec position(UObject* o)
    {
        Call c(o, L"K2_GetActorLocation");
        auto* p = c.field(L"ReturnValue");
        if (!p->IsA<FStructProperty>() || p->GetElementSize() != 12)
        {
            throw std::runtime_error("mom_vector_layout");
        }
        c.run();
        Vec v;
        std::memcpy(v.data(), p->ContainerPtrToValuePtr<void>(c.data.data()), 12);
        return v;
    }

    UObject* world(UObject* actor)
    {
        return is_uobject_valid_fast(actor) ? static_cast<AActor*>(actor)->GetWorld() : nullptr;
    }

    UObject* owning_world(UObject* actor)
    {
        if (!is_uobject_valid_fast(actor))
        {
            return nullptr;
        }
        // UE4SS's AActor::GetWorld walks Outers and returns a streamed package's
        // UWorld. The level's OwningWorld identifies the live persistent world.
        auto* level = actor->GetOuterPrivate();
        auto* owner = try_read_reflected_object_property_value(level, L"OwningWorld");
        return is_uobject_valid_fast(owner) ? owner : world(actor);
    }

    UObject* loaded(const wchar_t* path)
    {
        // Resolve through the engine's soft-reference hash. UE4SS's global lookup
        // caches raw objects discovered even while their packages are still loading.
        return find_loaded_runtime_asset(path);
    }

    uint8_t reactor_command(UObject* reactor, const uint8_t* value)
    {
        if (!is_uobject_valid_fast(reactor))
        {
            throw std::runtime_error("invalid_chest_reactor");
        }
        auto* p = reactor->GetPropertyByNameInChain(L"m_Command");
        if (!p || p->GetElementSize() != 1 ||
            (p->GetClass().GetName() != L"EnumProperty" &&
             p->GetClass().GetName() != L"ByteProperty"))
        {
            throw std::runtime_error("chest_reactor_command_layout");
        }
        auto* storage = p->ContainerPtrToValuePtr<uint8_t>(reactor);
        if (value)
        {
            *storage = *value;
        }
        return *storage;
    }

    FMulticastScriptDelegate* chest_delegate(UObject* reactor)
    {
        if (!is_uobject_valid_fast(reactor))
        {
            throw std::runtime_error("invalid_chest_reactor");
        }
        auto* p = reactor->GetPropertyByNameInChain(L"OnReactorDoCommand");
        if (!p || !p->IsA<FMulticastDelegateProperty>() ||
            p->GetElementSize() != sizeof(FMulticastScriptDelegate))
        {
            throw std::runtime_error("chest_reactor_delegate_layout");
        }
        return p->ContainerPtrToValuePtr<FMulticastScriptDelegate>(reactor);
    }

    bool active_chest(UObject* chest)
    {
        if (!is_uobject_valid_fast(chest))
        {
            return false;
        }
        auto* p = chest->GetPropertyByNameInChain(L"m_bIsActive");
        return p && p->IsA<FBoolProperty>() &&
               static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(chest);
    }
} // namespace kh3ap::music_chest
