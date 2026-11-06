#pragma once
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>

namespace kh3ap {
// Game-thread only. The name must have static lifetime. Metadata follows the
// concrete receiver class; weak serials reject GC and same-address slot reuse.
class OwnedFunctionCache {
public:
    explicit OwnedFunctionCache(const wchar_t* name) : name_(name) {}
    auto resolve(RC::Unreal::UObject* receiver) -> RC::Unreal::UFunction* {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(receiver)) return nullptr;
        auto* type = receiver->GetClassPrivate();
        if (!is_uobject_valid_fast(type)) return nullptr;
        auto* function = static_cast<UFunction*>(function_.Get());
        if (owner_.Get() == type && is_uobject_valid_fast(function)) return function;
        owner_ = {}; function_ = {};
        if (!is_uclass_child_of_fast(type, type)) return nullptr;
        function = receiver->GetFunctionByNameInChain(name_);
        if (!is_uobject_a_fast(function, UFunction::StaticClass()) ||
            !is_uclass_child_of_fast(type, static_cast<UClass*>(function->GetOuterPrivate()))) return nullptr;
        owner_ = make_uobject_weak(type);
        function_ = make_uobject_weak(function);
        // A platform without a verified serial allocator can still use the
        // synchronous result; it simply cannot retain it for the next call.
        return function;
    }
private:
    const wchar_t* name_;
    RC::Unreal::FWeakObjectPtr owner_, function_;
};
}
