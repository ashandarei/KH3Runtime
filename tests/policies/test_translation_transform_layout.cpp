#include "kh3ap/features/grants/vbonus/translation_transform_layout.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <span>

int main()
{
    constexpr std::byte sentinel{0xa5};
    std::array<std::byte, 80> storage;
    for (const std::size_t short_size : {0, 1, 39, 40, 43, 44, 47})
    {
        storage.fill(sentinel);
        assert(!kh3ap::translation_transform::write(std::span{storage}.subspan(1, short_size), 1.25,
                                                    -2.5, 3.75));
        assert(std::all_of(storage.begin(), storage.end(),
                           [&](std::byte value)
                           {
                               return value == sentinel;
                           }));
    }

    // An unaligned reflected buffer is valid; the writer must stay within it.
    for (const std::size_t valid_size : {48, 64})
    {
        storage.fill(sentinel);
        assert(kh3ap::translation_transform::write(std::span{storage}.subspan(1, valid_size), 1.25,
                                                   -2.5, 3.75));
        const std::array<float, 12> expected{0, 0, 0, 1, 1.25f, -2.5f, 3.75f, 0, 1, 1, 1, 0};
        assert(std::memcmp(storage.data() + 1, expected.data(), 48) == 0);
        assert(storage.front() == sentinel);
        for (std::size_t index = 49; index <= valid_size; ++index)
        {
            assert(storage[index] == std::byte{});
        }
        for (std::size_t index = valid_size + 1; index < storage.size(); ++index)
        {
            assert(storage[index] == sentinel);
        }
    }
}
