#pragma once
#include "UObject.hpp"
namespace RC::Unreal
{
    struct FWeakObjectPtr
    {
        UObject* object{};
        int serial{};
        auto operator=(UObject* value) -> void
        {
            object = value;
            serial = value ? value->serial : 0;
        }
        auto Get() const -> UObject*
        {
            return object && object->alive && object->serial == serial ? object : nullptr;
        }
    };
}
