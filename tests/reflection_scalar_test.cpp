#include "kh3ap/core/reflection/reflection_scalar.h"
#include <Unreal/FProperty.hpp>
#include <array>
#include <cassert>
#include <climits>
#include <cstring>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

using namespace kh3ap;
using RC::Unreal::FProperty;

namespace
{
    template <typename T> void check_signed_values()
    {
        for (const T value : {std::numeric_limits<T>::min(), T{-1}, T{0}, T{1},
                             std::numeric_limits<T>::max()})
        {
            // Unaligned input is deliberate: reflected/bytecode storage need not
            // have the alignment required by a typed pointer dereference.
            std::array<std::byte, sizeof(T) + 1> bytes{};
            std::memcpy(bytes.data() + 1, &value, sizeof(value));
            assert(read_signed_param_value(bytes.data() + 1, sizeof(T)) == value);
        }
    }
}

int main()
{
    std::array<unsigned char, 24> storage{};
    FProperty integer{{L"IntProperty"}, 3, 4};
    constexpr uint64_t pattern = 0x8877665544332211ULL;
    for (const int width : {1, 2, 4, 8})
    {
        storage.fill(0xa5);
        integer.size = width;
        assert(write_integral_function_param(&integer, storage.data(), pattern));
        for (size_t index = 0; index < storage.size(); ++index)
        {
            const auto expected = index >= 3 && index < 3 + static_cast<size_t>(width)
                ? static_cast<unsigned char>(pattern >> ((index - 3) * CHAR_BIT)) : 0xa5;
            assert(storage[index] == expected);
        }
        assert(write_integral_function_param(&integer, storage.data(), uint64_t(-1)));
        assert(read_signed_param_value(storage.data() + 3, width) == -1);
    }

    // Bad metadata must fail without touching the destination. The old clamp
    // wrote one byte for zero/negative widths and partially wrote large fields.
    storage.fill(0xa5);
    const auto unchanged = storage;
    for (const int width : {INT_MIN, -1, 0, 3, 5, 6, 7, 9, 16, INT_MAX})
    {
        integer.size = width;
        assert(!write_integral_function_param(&integer, storage.data(), pattern));
        assert(storage == unchanged);
        assert(!read_signed_param_value(storage.data(), width));
    }
    integer.size = 4;
    integer.offset = -1;
    assert(!write_integral_function_param(&integer, storage.data(), pattern));
    assert(storage == unchanged);
    assert(!write_integral_function_param(nullptr, storage.data(), pattern));
    assert(!write_integral_function_param(&integer, nullptr, pattern));
    assert(!read_signed_param_value(nullptr, 4));

    FProperty floating{{L"FloatProperty"}, 3, 4};
    assert(write_float_function_param(&floating, storage.data(), 1.25));
    float number{};
    std::memcpy(&number, storage.data() + 3, sizeof(number));
    assert(number == 1.25f && storage[2] == 0xa5 && storage[7] == 0xa5);
    floating.type.name = L"DoubleProperty";
    floating.size = 8;
    assert(write_float_function_param(&floating, storage.data(), -2.5));
    double wide_number{};
    std::memcpy(&wide_number, storage.data() + 3, sizeof(wide_number));
    assert(wide_number == -2.5 && storage[2] == 0xa5 && storage[11] == 0xa5);
    for (const auto* kind : {L"FloatProperty", L"DoubleProperty", L"StructProperty"})
    {
        floating.type.name = kind;
        for (const int width : {-1, 0, 1, 3, 4, 8, 16})
        {
            if ((floating.type.name == L"FloatProperty" && width == 4) ||
                (floating.type.name == L"DoubleProperty" && width == 8)) continue;
            floating.size = width;
            storage.fill(0xa5);
            assert(!write_float_function_param(&floating, storage.data(), 1.0));
            assert(storage == unchanged);
        }
    }
    assert(!write_float_function_param(nullptr, storage.data(), 1.0));
    assert(!write_float_function_param(&floating, nullptr, 1.0));
    check_signed_values<int8_t>();
    check_signed_values<int16_t>();
    check_signed_values<int32_t>();
    check_signed_values<int64_t>();

#ifdef _WIN32
    // Put every supported scalar immediately before an inaccessible page, so
    // widening either memcpy would fault rather than merely read adjacent bytes.
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    auto* allocation = static_cast<std::byte*>(VirtualAlloc(
        nullptr, 2 * info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(allocation);
    DWORD previous{};
    auto* boundary = allocation + info.dwPageSize;
    assert(VirtualProtect(boundary, info.dwPageSize, PAGE_NOACCESS, &previous));
    integer.offset = 0;
    for (const int width : {1, 2, 4, 8})
    {
        integer.size = width;
        assert(write_integral_function_param(&integer, boundary - width, uint64_t(-1)));
        assert(read_signed_param_value(boundary - width, width) == -1);
    }
    integer.size = 0;
    assert(!write_integral_function_param(&integer, boundary, pattern));
    assert(!read_signed_param_value(boundary, 0));
    assert(VirtualFree(allocation, 0, MEM_RELEASE));
#endif
}
