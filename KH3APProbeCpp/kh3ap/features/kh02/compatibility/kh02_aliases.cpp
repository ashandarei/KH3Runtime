#include "kh02_aliases.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/TArray.hpp>
extern "C" bool kh02_function_map_add(void*, unsigned long long, void*, unsigned long long, void*);

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t native_class_function_map_offset = 0x128;

    }

    void Kh02NativeAliases::add(const wchar_t* owner_path, const wchar_t* name, const wchar_t* prototype_path,
                                Thunk thunk)
    {
        auto* owner = kh02_require_loaded_object(owner_path);
        const std::wstring_view prototype(prototype_path);
        const auto separator = prototype.find(L':');
        if (!owner->IsA<UClass>() || separator == std::wstring_view::npos)
        {
            throw std::runtime_error("kh02_compat_owner_or_prototype_path");
        }
        auto* cls = static_cast<UClass*>(owner);
        const std::wstring prototype_owner(prototype.substr(0, separator));
        const std::wstring prototype_name(prototype.substr(separator + 1));
        auto* signature_owner = kh02_require_loaded_object(prototype_owner.c_str());
        if (!signature_owner->IsA<UClass>())
        {
            throw std::runtime_error("kh02_compat_signature_owner_class");
        }
        UFunction* proto{};
        const FName signature_name(prototype_name.c_str(), FNAME_Find);
        for (auto* candidate : static_cast<UClass*>(signature_owner)->ForEachFunctionInChain())
        {
            if (is_uobject_valid_fast(candidate) && candidate->GetNamePrivate() == signature_name)
            {
                proto = candidate;
                break;
            }
        }
        if (!is_uobject_valid_fast(cls) || !is_uobject_valid_fast(proto) || !(proto->GetFunctionFlags() & FUNC_Native))
        {
            throw std::runtime_error("kh02_compat_prototype:" + to_utf8(prototype_path));
        }
        if (proto->GetPathName() != prototype_path)
        {
            throw std::runtime_error("kh02_compat_prototype_owner_mismatch");
        }
        UFunction* anchor = nullptr;
        const FName alias_name(name, FNAME_Add);
        for (auto* existing : cls->ForEachFunction())
        {
            if (!is_uobject_valid_fast(existing))
            {
                continue;
            }
            if (existing->GetNamePrivate() == alias_name)
            {
                throw std::runtime_error("kh02_compat_alias_exists");
            }
            if (!anchor && existing->GetOuterPrivate() == cls)
            {
                anchor = existing;
            }
        }
        if (!anchor)
        {
            throw std::runtime_error("kh02_compat_function_map_anchor");
        }
        FStaticConstructObjectParameters args(UFunction::StaticClass(), cls);
        args.Name = alias_name;
        args.SetFlags = static_cast<EObjectFlags>(RF_Public | RF_Transient);
        auto* fn = UObjectGlobals::StaticConstructObject<UFunction*>(args);
        if (!is_uobject_valid_fast(fn) || fn->GetOuterPrivate() != cls)
        {
            throw std::runtime_error("kh02_compat_alias_construct");
        }
        // Borrow immutable property descriptors from a resident native
        // signature. This compatibility function owns no duplicate fields.
        fn->GetChildren() = proto->GetChildren();
        fn->GetPropertyLink() = proto->GetPropertyLink();
        fn->GetRefLink() = proto->GetRefLink();
        fn->GetDestructorLink() = proto->GetDestructorLink();
        fn->GetPostConstructLink() = proto->GetPostConstructLink();
        fn->GetPropertiesSize() = proto->GetPropertiesSize();
        fn->GetMinAlignment() = proto->GetMinAlignment();
        fn->GetFunctionFlags() = proto->GetFunctionFlags();
        fn->GetNumParms() = proto->GetNumParms();
        fn->GetParmsSize() = proto->GetParmsSize();
        fn->GetReturnValueOffset() = proto->GetReturnValueOffset();
        fn->GetFirstPropertyToInit() = proto->GetFirstPropertyToInit();
        fn->SetFuncPtr(thunk ? thunk : proto->GetFuncPtr());
        fn->SetRootSet();
        fn->GetNext() = cls->GetChildren();
        cls->GetChildren() = fn;
        bool linked = false;
        for (auto* item : cls->ForEachFunction())
        {
            if (item == fn)
            {
                linked = true;
                break;
            }
        }
        if (!linked)
        {
            throw std::runtime_error("kh02_compat_alias_lookup");
        }
        uint64_t anchor_key{}, key{};
        const auto anchor_name = anchor->GetNamePrivate(), function_name = fn->GetNamePrivate();
        std::memcpy(&anchor_key, &anchor_name, sizeof(anchor_key));
        std::memcpy(&key, &function_name, sizeof(key));
        // This verified EGS class layout places FuncMap at +0x128. The ABI
        // helper validates a resident name/function pair before any insertion.
        if (!kh02_function_map_add(reinterpret_cast<std::byte*>(cls) + native_class_function_map_offset, anchor_key,
                                   anchor, key, fn))
        {
            throw std::runtime_error("kh02_compat_function_map_layout");
        }
        descriptors.push_back(make_uobject_weak(fn));
    }

    void Kh02NativeAliases::track_descriptor(UObject* object)
    {
        descriptors.push_back(make_uobject_weak(object));
    }

    auto Kh02NativeAliases::empty() const -> bool
    {
        return descriptors.empty();
    }

    auto Kh02NativeAliases::size() const -> std::size_t
    {
        return descriptors.size();
    }

} // namespace kh3ap
