#include "kh3ap/features/kh02/core/kh02_call_parameters.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
    size_t allocations{};
}

void* operator new(std::size_t size)
{
    if (void* result = std::malloc(size ? size : 1))
    {
        ++allocations;
        return result;
    }
    throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

int main()
{
    using kh3ap::Kh02CallParameters;
    static_assert(Kh02CallParameters::native_alignment == 16);
    static_assert(alignof(Kh02CallParameters) >= 16);
    Kh02CallParameters outer;
    assert(outer.empty());
    assert(outer.data() == nullptr);

    // All supported small records must be initialized and correctly aligned,
    // with no allocation in the synchronous reflected-call path.
    const auto small_allocations_before = allocations;
    for (size_t size = 1; size <= Kh02CallParameters::inline_capacity; ++size)
    {
        outer.resize(size);
        assert(outer.size() == size);
        assert(reinterpret_cast<uintptr_t>(outer.data()) % 16 == 0);
        for (size_t index = 0; index < size; ++index)
        {
            assert(outer.data()[index] == std::byte{});
            outer.data()[index] = std::byte{0xff};
        }
    }
    assert(allocations == small_allocations_before);

    // A nested engine callback must not overwrite the outer call's parameters.
    {
        Kh02CallParameters nested;
        nested.resize(64);
        nested.data()[0] = std::byte{0x42};
        assert(outer.data()[0] == std::byte{0xff});
        assert(nested.data() != outer.data());
    }

    // Unusually large signatures remain supported without an artificial cap.
    outer.resize(4096);
    assert(outer.size() == 4096);
    assert(reinterpret_cast<uintptr_t>(outer.data()) % 16 == 0);
    for (size_t index = 0; index < outer.size(); ++index)
    {
        assert(outer.data()[index] == std::byte{});
        outer.data()[index] = std::byte{0x11};
    }
    outer.resize(16);
    for (size_t index = 0; index < outer.size(); ++index)
    {
        assert(outer.data()[index] == std::byte{});
    }
    outer.resize(0);
    assert(outer.empty());
    assert(outer.data() == nullptr);

    // An eight-byte prefix previously placed the inline buffer at an address
    // accepted by max_align_t but rejected by KH3's aligned Transform reads.
    struct alignas(16) PrefixedCall
    {
        uint64_t prefix{};
        Kh02CallParameters parameters;
    } prefixed;

    for (const size_t size : {64, 257, 511, 4096})
    {
        prefixed.parameters.resize(size);
        auto* bytes = prefixed.parameters.data();
        assert(bytes != nullptr && reinterpret_cast<uintptr_t>(bytes) % 16 == 0);
        for (size_t index = 0; index < size; ++index)
        {
            assert(bytes[index] == std::byte{});
        }
        bytes[size - 1] = std::byte{0x7f};
        assert(prefixed.parameters.data()[size - 1] == std::byte{0x7f});
    }

    // Compare allocation counts for a representative sequence of small engine
    // calls. This is an allocation check, not a claim about gameplay frame rate.
    const auto vector_before = allocations;
    for (size_t iteration = 0; iteration < 10000; ++iteration)
    {
        std::vector<std::byte> original(64);
        original[iteration % original.size()] = std::byte{0x7f};
        assert(original[iteration % original.size()] == std::byte{0x7f});
    }
    const auto vector_allocations = allocations - vector_before;
    const auto inline_before = allocations;
    for (size_t iteration = 0; iteration < 10000; ++iteration)
    {
        Kh02CallParameters local;
        local.resize(64);
        local.data()[iteration % local.size()] = std::byte{0x7f};
        assert(local.data()[iteration % local.size()] == std::byte{0x7f});
    }
    const auto inline_allocations = allocations - inline_before;
    assert(inline_allocations == 0);
    std::cout << "small-call allocations: vector=" << vector_allocations << " inline=" << inline_allocations << '\n';
}
