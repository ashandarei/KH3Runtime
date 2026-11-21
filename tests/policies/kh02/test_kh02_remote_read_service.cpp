#include "kh3ap/features/kh02/transport/kh02_remote_read_service.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>

int main()
{
    kh02asset::RemoteReadService service;
    std::array<std::byte, 8> bytes{};
    unsigned loads{};
    uint64_t loaded_offset{};
    uint32_t loaded_length{};
    auto load = [&](uint32_t, uint64_t offset, uint32_t length, void* destination)
    {
        // The service must release its cache lock before calling transport code.
        // Re-entering a settings/status operation must not deadlock the loader.
        service.snapshot();
        ++loads;
        loaded_offset = offset;
        loaded_length = length;
        auto* output = static_cast<std::byte*>(destination);
        for (uint32_t index = 0; index < length; ++index)
        {
            output[index] = static_cast<std::byte>((offset + index) % 256);
        }
        return true;
    };

    assert(service.read(1, 32, L"header.uasset", 4, bytes.size(), bytes.data(), load));
    assert(loads == 1 && loaded_offset == 0 && loaded_length == 32);
    assert(bytes.front() == std::byte{4} && bytes.back() == std::byte{11});
    assert(service.read(1, 32, L"header.uasset", 8, bytes.size(), bytes.data(), load));
    assert(loads == 1 && bytes.front() == std::byte{8});

    // Export cache admission excludes the trailing package tag. A footer read
    // must still reach transport instead of indexing beyond the cached prefix.
    assert(service.read(2, 12, L"exports.uexp", 0, bytes.size(), bytes.data(), load));
    assert(loads == 2 && loaded_length == 8);
    assert(service.read(2, 12, L"exports.uexp", 2, 4, bytes.data(), load));
    assert(loads == 2 && bytes.front() == std::byte{2});
    assert(service.read(2, 12, L"exports.uexp", 8, 4, bytes.data(), load));
    assert(loads == 3 && loaded_offset == 8 && loaded_length == 4);

    assert(service.read(3, 100'000, L"bulk.ubulk", 20, 4, bytes.data(), load));
    assert(loads == 4);
    assert(service.read(3, 100'000, L"bulk.ubulk", 20, 4, bytes.data(), load));
    assert(loads == 4);
    service.configure_ranges(false);
    assert(service.read(3, 100'000, L"bulk.ubulk", 20, 4, bytes.data(), load));
    assert(service.read(3, 100'000, L"bulk.ubulk", 20, 4, bytes.data(), load));
    assert(loads == 6);

    service.configure_exports(false);
    assert(service.read(2, 12, L"exports.uexp", 0, bytes.size(), bytes.data(), load));
    assert(loads == 7);
    service.clear();
    const auto [headers, exports, exports_enabled, ranges, ranges_enabled] = service.snapshot();
    assert(headers.bytes == 0 && exports.bytes == 0 && ranges.bytes == 0);
    assert(!exports_enabled && !ranges_enabled);

    auto failed_load = [](uint32_t, uint64_t, uint32_t, void*) { return false; };
    assert(!service.read(4, 32, L"retry.uasset", 0, 4, bytes.data(), failed_load));
    assert(service.read(4, 32, L"retry.uasset", 0, 4, bytes.data(), load));
    assert(loads == 8);

    service.configure_exports(true);
    const std::vector<uint8_t> prepared(12, 42);
    service.cache_prepared_exports(5, prepared);
    assert(service.read(5, prepared.size(), L"paired.uexp", 0, bytes.size(), bytes.data(), load));
    assert(loads == 8 && bytes.front() == std::byte{42} && bytes.back() == std::byte{42});
    assert(service.read(5, prepared.size(), L"paired.uexp", 8, 4, bytes.data(), load));
    assert(loads == 9 && loaded_offset == 8 && loaded_length == 4);
    service.configure_exports(false);
    service.cache_prepared_exports(5, prepared);
    assert(std::get<1>(service.snapshot()).bytes == 0);

    kh02asset::FileReadCache bounded_cache(12);
    const auto fill = [](std::span<std::byte>) { return true; };
    assert(bounded_cache.read(1, 4, 0, {}, fill));
    bounded_cache.store_prepared(2, std::vector<uint8_t>(bytes.size()));
    assert(bounded_cache.read(3, 4, 0, {}, fill));
    assert(bounded_cache.contains(1) && !bounded_cache.contains(2) && bounded_cache.contains(3));

    service.configure_exports(true);
    constexpr auto large_size = kh02asset::export_cache_max_file + 12;
    service.cache_prepared_exports(6, std::vector<uint8_t>(large_size, 57));
    assert(service.read(6, large_size, L"large.uexp", 0, bytes.size(), bytes.data(), load));
    assert(service.read(6, large_size, L"large.uexp", large_size - 12, bytes.size(), bytes.data(), load));
    assert(loads == 9 && bytes.front() == std::byte{57} && bytes.back() == std::byte{57});
    assert(std::get<1>(service.snapshot()).bytes == 0);
    assert(service.read(6, large_size, L"large.uexp", large_size - 4, 4, bytes.data(), load));
    assert(loads == 10 && loaded_offset == large_size - 4 && loaded_length == 4);

    service.configure_ranges(true);
    constexpr uint32_t window = 64 * 1024;
    constexpr uint32_t file_size = window + 104;
    assert(service.read(7, file_size, L"objects.uexp", 19, bytes.size(), bytes.data(), load));
    assert(loads == 11 && loaded_offset == 0 && loaded_length == window);
    assert(service.read(7, file_size, L"objects.uexp", 91, bytes.size(), bytes.data(), load));
    assert(loads == 11 && bytes.front() == std::byte{91} && bytes.back() == std::byte{98});
    assert(service.read(7, file_size, L"objects.uexp", window - 4, bytes.size(), bytes.data(), load));
    assert(loads == 12 && loaded_offset == window - 4 && loaded_length == bytes.size());
    assert(service.read(7, file_size, L"objects.uexp", window + 1, bytes.size(), bytes.data(), load));
    assert(loads == 13 && loaded_offset == window && loaded_length == 100);
    assert(service.read(7, file_size, L"objects.uexp", file_size - 8, bytes.size(), bytes.data(), load));
    assert(loads == 14 && loaded_offset == file_size - 8 && loaded_length == bytes.size());
    std::cout << "KH0.2 remote read service passed\n";
}
