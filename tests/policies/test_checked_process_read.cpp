#include "kh3ap/core/memory/checked_process_read.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>

int main()
{
    using kh3ap::try_read_current_process_value;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const size_t page_bytes = system.dwPageSize;
    auto* allocation = static_cast<std::byte*>(VirtualAlloc(
        nullptr, page_bytes * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(allocation);
    const auto address = reinterpret_cast<uintptr_t>(allocation);
    uint64_t expected = 0x123456789abcdef0;
    std::memcpy(allocation, &expected, sizeof(expected));
    assert(try_read_current_process_value<uint64_t>(address) == expected);
    expected = 9;
    std::memcpy(allocation, &expected, sizeof(expected));
    assert(try_read_current_process_value<uint64_t>(address) == expected);

    DWORD old_protection{};
    assert(VirtualProtect(allocation + page_bytes, page_bytes, PAGE_NOACCESS, &old_protection));
    assert(!try_read_current_process_value<uint64_t>(address + page_bytes));
    // Discard partial reads: a readable first page is not sufficient.
    assert(!try_read_current_process_value<uint64_t>(address + page_bytes - 4));
    assert(!try_read_current_process_value<uint64_t>(0));
    assert(!try_read_current_process_value<uint64_t>(1));
    assert(!try_read_current_process_value<uint64_t>((std::numeric_limits<uintptr_t>::max)() - 3));

    assert(VirtualProtect(allocation + page_bytes, page_bytes, PAGE_READWRITE | PAGE_GUARD,
                         &old_protection));
    assert(!try_read_current_process_value<uint64_t>(address + page_bytes));
    MEMORY_BASIC_INFORMATION guarded{};
    assert(VirtualQuery(allocation + page_bytes, &guarded, sizeof(guarded)) == sizeof(guarded));
    assert((guarded.Protect & PAGE_GUARD) != 0);

    assert(VirtualFree(allocation, 0, MEM_RELEASE));
    assert(!try_read_current_process_value<uint64_t>(address));
}
