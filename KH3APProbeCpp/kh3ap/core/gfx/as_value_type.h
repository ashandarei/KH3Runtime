#pragma once

#include <cstdint>

namespace kh3ap
{
    // Reflected ScaleformUI ASValue.ASType tags, not native GFx::Value type IDs.
    enum class ASValueType : uint8_t
    {
        Undefined = 0,
        Null = 1,
        Boolean = 2,
        Int = 3,
        Number = 4,
        String = 5,
        StringW = 6,
        Object = 7,
        DisplayObject = 8,
    };

    constexpr auto as_value_type_id(ASValueType type) -> uint8_t
    {
        return static_cast<uint8_t>(type);
    }
} // namespace kh3ap
