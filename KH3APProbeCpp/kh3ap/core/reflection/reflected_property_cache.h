#pragma once
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <array>
#include <cstring>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Cache immutable metadata under the concrete class's weak serial. Values
    // remain live reads; unload/replacement invalidates the associated layout.
    template <size_t Capacity> class ReflectedPropertyCache
    {
        static_assert(Capacity > 0);
        struct Entry
        {
            RC::Unreal::FWeakObjectPtr owner;
            std::wstring name;
            RC::Unreal::FProperty* property{};
            bool object_checked{}, object_valid{};
        };

    public:
        auto resolve(RC::Unreal::UObject* object, std::wstring_view name) -> RC::Unreal::FProperty*
        {
            auto* entry = find(object, name);
            return entry ? entry->property : nullptr;
        }
        auto resolve_object(RC::Unreal::UObject* object, std::wstring_view name)
            -> RC::Unreal::FProperty*
        {
            auto* entry = find(object, name);
            if (!entry)
            {
                return nullptr;
            }
            if (!entry->object_checked)
            {
                entry->object_valid =
                    entry->property->GetClass().GetName() == L"ObjectProperty" &&
                    entry->property->GetElementSize() == sizeof(RC::Unreal::UObject*);
                entry->object_checked = true;
            }
            return entry->object_valid ? entry->property : nullptr;
        }
        auto read_object(RC::Unreal::UObject* object, std::wstring_view name)
            -> RC::Unreal::UObject*
        {
            auto* property = resolve_object(object, name);
            if (!property)
            {
                return nullptr;
            }
            RC::Unreal::UObject* value{};
            std::memcpy(&value, property->ContainerPtrToValuePtr<void>(object), sizeof(value));
            return is_uobject_valid_fast(value) ? value : nullptr;
        }

    private:
        auto find(RC::Unreal::UObject* object, std::wstring_view name) -> Entry*
        {
            using namespace RC::Unreal;
            if (!is_uobject_valid_fast(object))
            {
                return nullptr;
            }
            auto* type = object->GetClassPrivate();
            if (!is_uobject_valid_fast(type))
            {
                return nullptr;
            }
            for (auto& entry : entries_)
            {
                if (entry.owner.Get() == type && entry.name == name)
                {
                    return &entry;
                }
            }
            if (!is_uclass_child_of_fast(type, type))
            {
                return nullptr;
            }
            const std::wstring owned_name{name};
            auto* property = object->GetPropertyByNameInChain(owned_name.c_str());
            if (!property || property->GetOffset_Internal() < 0 || property->GetElementSize() <= 0)
            {
                return nullptr;
            }
            auto& entry = entries_[next_++ % Capacity];
            entry = {make_uobject_weak(type), owned_name, property};
            return &entry;
        }
        std::array<Entry, Capacity> entries_{};
        size_t next_{};
    };
} // namespace kh3ap
