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
    // Resolve an exact reflected path through the receiver's class hierarchy.
    // Unlike a name-only lookup this preserves calls to a specified base function
    // when a derived class overrides the same name. Game thread only.
    template <size_t Capacity = 8> class ReceiverFunctionCache
    {
        static_assert(Capacity > 0);
        struct Entry
        {
            std::wstring path;
            RC::Unreal::FWeakObjectPtr type, function;
        };
        std::array<Entry, Capacity> entries_{};
        size_t next_{};

    public:
        auto resolve(RC::Unreal::UObject* receiver, std::wstring_view path)
            -> RC::Unreal::UFunction*
        {
            using namespace RC::Unreal;
            if (!is_uobject_valid_fast(receiver))
            {
                return nullptr;
            }
            auto* type = receiver->GetClassPrivate();
            if (!is_uobject_valid_fast(type) || !is_uclass_child_of_fast(type, type))
            {
                return nullptr;
            }
            for (const auto& entry : entries_)
            {
                if (entry.path != path || entry.type.Get() != type)
                {
                    continue;
                }
                auto* function = static_cast<UFunction*>(entry.function.Get());
                if (is_uobject_valid_fast(function))
                {
                    return function;
                }
            }
            const auto separator = path.rfind(L':');
            if (separator == std::wstring_view::npos || separator + 1 == path.size())
            {
                return nullptr;
            }
            const auto leaf = path.substr(separator + 1);
            for (auto* function : type->ForEachFunctionInChain())
            {
                if (!is_uobject_valid_fast(function) || function->GetName() != leaf ||
                    function->GetPathName() != path)
                {
                    continue;
                }
                entries_[next_++ % Capacity] = {std::wstring{path}, make_uobject_weak(type),
                                                make_uobject_weak(function)};
                return function;
            }
            // Do not retain misses: a Blueprint may still be loading.
            return nullptr;
        }
    };
} // namespace kh3ap
