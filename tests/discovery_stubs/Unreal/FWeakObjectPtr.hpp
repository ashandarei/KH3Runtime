#pragma once
#include "Runtime.hpp"

namespace RC::Unreal
{
    struct FWeakObjectPtr
    {
        int32_t index{-1}, serial{};
        auto operator=(UObject* object) -> void
        {
            index = object ? object->index : -1;
            serial = object ? UObjectArray::items[index].serial : 0;
        }
        auto Get() const -> UObject*
        {
            if (index < 0 || index >= UObjectArray::GetNumElements()) return nullptr;
            const auto& item = UObjectArray::items[index];
            return serial > 0 && item.alive && item.serial == serial ? item.object : nullptr;
        }
        auto SerialNumbersMatch(FUObjectItem* item) const -> bool
        { return item && item->serial == serial; }
    };
}
