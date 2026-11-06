#include "kh3ap/core/reflection/reflection_scalar.h"

#include <Unreal/FProperty.hpp>
#include <bit>
#include <cstring>

namespace kh3ap
{
    auto write_integral_function_param(RC::Unreal::FProperty* property, void* params,
                                       uint64_t value) -> bool
    {
        if (!property || !params || property->GetOffset_Internal() < 0)
        {
            return false;
        }

        const int element_size = property->GetElementSize();
        if (element_size != 1 && element_size != 2 && element_size != 4 && element_size != 8)
        {
            return false;
        }
        void* const value_ptr = property->ContainerPtrToValuePtr<void>(params);
        if (!value_ptr)
        {
            return false;
        }

        // KH3's integer and enum storage uses the low bytes on little-endian x64.
        static_assert(std::endian::native == std::endian::little);
        std::memcpy(value_ptr, &value, static_cast<size_t>(element_size));
        return true;
    }

    auto write_float_function_param(RC::Unreal::FProperty* property, void* params,
                                    double value) -> bool
    {
        if (!property || !params || property->GetOffset_Internal() < 0)
        {
            return false;
        }

        const auto property_class = property->GetClass().GetName();
        const int element_size = property->GetElementSize();
        if (property_class == L"FloatProperty" && element_size == sizeof(float))
        {
            const float float_value = static_cast<float>(value);
            std::memcpy(property->ContainerPtrToValuePtr<void>(params), &float_value,
                        sizeof(float_value));
            return true;
        }
        if (property_class == L"DoubleProperty" && element_size == sizeof(double))
        {
            std::memcpy(property->ContainerPtrToValuePtr<void>(params), &value, sizeof(value));
            return true;
        }
        return false;
    }

    auto read_signed_param_value(const void* value_ptr, int element_size)
        -> std::optional<int64_t>
    {
        if (!value_ptr)
        {
            return std::nullopt;
        }
        // Copy into the actual signed width before promotion. Copying four bytes
        // into a zeroed int64_t turns -1 into 4294967295 instead of sign extending.
        const auto read = [value_ptr]<typename T>() -> int64_t
        {
            T value{};
            std::memcpy(&value, value_ptr, sizeof(value));
            return value;
        };
        switch (element_size)
        {
        case 1: return read.operator()<int8_t>();
        case 2: return read.operator()<int16_t>();
        case 4: return read.operator()<int32_t>();
        case 8: return read.operator()<int64_t>();
        default: return std::nullopt;
        }
    }
}
