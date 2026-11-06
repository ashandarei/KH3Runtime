#pragma once
#include "UFunction.hpp"
namespace RC::Unreal
{
    class FWeakObjectPtr
    {
    public:
        auto operator=(UObject* object) -> FWeakObjectPtr&
        {
            value = object;
            serial = object ? object->serial : 0;
            return *this;
        }
        auto Get() const -> UObject*
        {
            return value && serial && value->valid && value->serial == serial ? value : nullptr;
        }

    private:
        UObject* value{};
        int serial{};
    };
} // namespace RC::Unreal
