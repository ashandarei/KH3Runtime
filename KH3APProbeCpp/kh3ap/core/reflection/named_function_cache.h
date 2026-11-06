#pragma once
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Game-thread lookup with the same override selection as
    // GetFunctionByNameInChain. Cache metadata by concrete receiver class,
    // never a pawn; weak serials reject class/function unload and slot reuse.
    template <size_t Capacity> class NamedFunctionCache
    {
        static_assert(Capacity > 0);
        struct Entry
        {
            std::wstring name;
            RC::Unreal::FWeakObjectPtr type, function;
        };
        std::array<Entry, Capacity> entries_{};
        size_t next_{};

    public:
        auto resolve(RC::Unreal::UObject* receiver, std::wstring_view name)
            -> RC::Unreal::UFunction*
        {
            using namespace RC::Unreal;
            if (!is_uobject_valid_fast(receiver))
            {
                return nullptr;
            }
            auto* type = receiver->GetClassPrivate();
            if (!is_uobject_valid_fast(type))
            {
                return nullptr;
            }
            for (const auto& entry : entries_)
            {
                if (entry.name == name && entry.type.Get() == type)
                {
                    if (auto* function = static_cast<UFunction*>(entry.function.Get());
                        is_uobject_valid_fast(function))
                    {
                        return function;
                    }
                }
            }
            if (!is_uclass_child_of_fast(type, type))
            {
                return nullptr;
            }
            const std::wstring owned_name{name};
            auto* function = receiver->GetFunctionByNameInChain(owned_name.c_str());
            if (!is_uobject_a_fast(function, UFunction::StaticClass()) ||
                !is_uclass_child_of_fast(type, static_cast<UClass*>(function->GetOuterPrivate())))
            {
                return nullptr;
            }
            // A missing function can arrive after asynchronous Blueprint load.
            // Only successful lookups consume a bounded cache entry.
            entries_[next_++ % Capacity] = {owned_name, make_uobject_weak(type),
                                           make_uobject_weak(function)};
            return function;
        }
    };
}
