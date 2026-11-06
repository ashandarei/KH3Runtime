#include "kh3ap/features/kh02/transport/kh02_file_cache.h"
#include <array>
#include <cassert>
#include <limits>

int main()
{
    using kh02asset::cache_export_read;
    assert(cache_export_read(L"/content/z/example.uexp", 4096, 0, 4092, false));
    assert(!cache_export_read(L"/content/z/example.uexp", 4096, 0, 4096, false)); // Footer bypasses cache.
    assert(!cache_export_read(L"/content/z/example.uexp", 4096, 4090, 4, true));
    assert(!cache_export_read(L"/content/z/example.uexp", 4096, 0, 128, false));
    assert(!cache_export_read(L"/content/z/example.uexp", 4096, 16, 4080, false));
    assert(cache_export_read(L"/content/z/example.uexp", 4096, 16, 128, true));
    assert(!cache_export_read(L"/content/z/example.ubulk", 4096, 0, 4096, false));
    assert(cache_export_read(L"/content/z/example.uexp", 1048576, 0, 1048572, false));
    assert(!cache_export_read(L"/content/z/example.uexp", 2097152, 0, 2097148, false));
    assert(!cache_export_read(L"/content/z/example.uexp", 0, 0, 0, false));
    kh02asset::FileReadCache cache(8);
    std::array<std::byte, 2> output{};
    int reads = 0;
    auto load = [&](std::span<std::byte> bytes)
    {
        ++reads;
        for (size_t index = 0; index < bytes.size(); ++index)
        {
            bytes[index] = std::byte(index + 1);
        }
        return true;
    };

    assert(cache.read(1, 4, 1, output, load));
    assert((output == std::array{std::byte{2}, std::byte{3}}));
    assert(cache.read(1, 4, 2, output, load));
    assert((output == std::array{std::byte{3}, std::byte{4}}));
    assert(reads == 1 && cache.stats().reused_bytes == 2); // Distinct ranges share one validated file.

    assert(!cache.read(1, 4, 3, output, load));
    assert(!cache.read(1, 4, std::numeric_limits<uint64_t>::max(), output, load));
    assert(!cache.read(1, 5, 0, output, load)); // Same id cannot silently change size.
    assert(!cache.read(2, 9, 0, output, load));
    assert(reads == 1);

    assert(cache.read(2, 4, 0, output, load));
    assert(cache.read(1, 4, 0, output, load)); // Refresh id 1, so id 2 is oldest.
    assert(!cache.read(3, 4, 0, output, [](auto) { return false; }));
    assert(cache.stats().bytes == 8 && cache.stats().evictions == 0);
    assert(cache.read(2, 4, 0, output, load)); // Failure did not evict id 2.
    assert(reads == 2);
    assert(cache.read(1, 4, 0, output, load));
    assert(cache.read(3, 4, 0, output, load));
    assert(cache.stats().bytes == 8 && cache.stats().evictions == 1);
    assert(cache.read(1, 4, 0, output, load));
    assert(reads == 3);
    assert(cache.read(2, 4, 0, output, load)); // Evicted id must be fetched again.
    assert(reads == 4);

    cache.clear(); // New provider/catalog must not reuse old payloads.
    assert(cache.stats().bytes == 0 && cache.stats().hits == 0);
    assert(cache.read(1, 4, 0, output,
                      [&](auto bytes)
                      {
                          ++reads;
                          std::fill(bytes.begin(), bytes.end(), std::byte{9});
                          return true;
                      }));
    assert((output == std::array{std::byte{9}, std::byte{9}}));
    assert(reads == 5);

    using kh02asset::cache_range_read;
    assert(cache_range_read(L"mesh.uexp", 1024 * 1024, 100, 32768));
    assert(!cache_range_read(L"mesh.uexp", 1024 * 1024, 100, 32769));
    assert(cache_range_read(L"map.umap", 200000, 0, 200000));
    assert(cache_range_read(L"mesh.uasset", 200000, 0, 200000));
    assert(!cache_range_read(L"mesh.uexp", 200000, 0, 200000));
    assert(!cache_range_read(L"map.umap", 200000, 100, 199900));
    assert(!cache_range_read(L"map.umap", 300000, 0, 300000));
    assert(!cache_range_read(L"mesh.uexp", 128, 127, 2));
    assert(!cache_range_read(L"mesh.uexp", 128, std::numeric_limits<uint64_t>::max(), 1));
    assert(!cache_range_read(L"mesh.uexp", 128, 0, 0));

    // Range identity includes file, offset and length; overlapping reads cannot
    // accidentally return the wrong bytes. A hit performs no source read.
    kh02asset::RangeReadCache ranges(16, 3);
    using kh02asset::ReadRange;
    const ReadRange original{1, 40, 2};
    assert(ranges.read(original, 2, 0, output, load));
    assert(ranges.read(original, 2, 0, output,
                       [](auto)
                       {
                           assert(false);
                           return false;
                       }));
    assert(ranges.read(ReadRange{1, 41, 2}, 2, 0, output, load));
    assert(ranges.read(ReadRange{2, 40, 2}, 2, 0, output, load));
    assert(ranges.contains(original));
    std::array<std::byte, 1> one{};
    assert(ranges.read(ReadRange{1, 40, 1}, 1, 0, one, load));
    assert(!ranges.contains(original)); // Metadata cap applies before byte cap.
    assert(ranges.stats().bytes == 5 && ranges.stats().evictions == 1);
    assert(!ranges.read(original, 2, 0, output, [](auto) { return false; }));
    assert(ranges.stats().bytes == 5 && ranges.stats().evictions == 1);
    ranges.clear();
    assert(!ranges.contains(ReadRange{1, 40, 1}) && ranges.stats().bytes == 0);
    kh02asset::RangeReadCache disabled(16, 0);
    assert(!disabled.read(original, 2, 0, output,
                          [](auto)
                          {
                              assert(false);
                              return false;
                          }));
}
