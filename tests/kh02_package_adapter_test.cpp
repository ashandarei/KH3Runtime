#include "kh3ap/features/kh02/transport/kh02_package_format.h"
#include "kh3ap/features/kh02/transport/kh02_index_state.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <atomic>

using namespace kh02asset;
using namespace kh02asset::package;

auto synthetic_header() -> Bytes
{
    Bytes result(128);
    put<uint32_t>(result, 0, 0x9e2a83c1);
    put<int32_t>(result, 4, -7);
    put<int32_t>(result, 28, 1);
    put<int32_t>(result, 37, 2);
    put<int32_t>(result, 41, 128);
    for (const std::string value : {"None", "/Game/Test/Asset"})
    {
        append<int32_t>(result, static_cast<int32_t>(value.size() + 1));
        result.insert(result.end(), value.begin(), value.end());
        result.push_back(0);
        const auto hashes = name_hashes(value, false);
        append(result, hashes[0]);
        append(result, hashes[1]);
    }
    put<int32_t>(result, 57, static_cast<int32_t>(result.size()));
    put<int32_t>(result, 65, static_cast<int32_t>(result.size()));
    return result;
}

int main()
{
    const auto header = synthetic_header();
    Bytes exports;
    const std::string path = "/Game/Other/Thing";
    append<int32_t>(exports, static_cast<int32_t>(path.size() + 1));
    exports.insert(exports.end(), path.begin(), path.end());
    exports.push_back(0);
    const auto adapted = adapt_package("Test/Synthetic", header, exports);
    assert(parse(adapted.header).names[1].ascii == "/Game/Zest/Asset");
    assert(adapted.header.size() == header.size() && adapted.exports.size() == exports.size());
    assert(std::string(adapted.exports.begin() + 4, adapted.exports.end() - 1) ==
           "/Game/Zther/Thing");
    for (std::size_t size = 0; size < header.size(); ++size)
    {
        bool rejected{};
        try
        {
            adapt_package("Test/Truncated", std::span(header).first(size), {});
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        assert(rejected);
    }
    auto corrupt = header;
    corrupt[corrupt.size() - 1] ^= 1;
    bool rejected{};
    try
    {
        adapt_package("Test/BadNameHash", corrupt, {});
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    assert(rejected);
    const auto prefix = std::string(source_content_prefix);
    configure_source_identity(std::string(64, 'a'));
    publish_index(build_index({{std::string(source_control_file), 1},
                               {prefix + "Test/Synthetic.uasset", header.size()},
                               {prefix + "Test/Synthetic.uexp", exports.size()}},
                              std::string(64, 'a')));
    uint32_t header_id{}, export_id{};
    for (uint32_t i = 0; i < asset_index().files.size(); ++i)
    {
        if (asset_index().files[i].suffix.ends_with(L".uasset"))
        {
            header_id = i;
        }
        if (asset_index().files[i].suffix.ends_with(L".uexp"))
        {
            export_id = i;
        }
    }
    std::atomic<unsigned> preparations{};
    const auto raw = [&](uint32_t id, uint64_t at, uint32_t count, void* output)
    {
        const auto& bytes = id == header_id ? header : exports;
        if (at == 0 && count == header.size() && id == header_id)
        {
            ++preparations;
        }
        if (at > bytes.size() || count > bytes.size() - at)
        {
            return false;
        }
        if (count)
        {
            std::memcpy(output, bytes.data() + at, count);
        }
        return true;
    };
    PackageAdapter cache;
    std::vector<std::thread> readers;
    for (int i = 0; i < 8; ++i)
    {
        readers.emplace_back(
            [&]
            {
                for (std::size_t at = 0; at < exports.size(); ++at)
                {
                    uint8_t byte{};
                    assert(cache.read(export_id, at, 1, &byte, raw));
                    assert(byte == adapted.exports[at]);
                }
            });
    }
    for (auto& reader : readers)
    {
        reader.join();
    }
    assert(preparations == 1);
    cache.clear();
    uint8_t fresh{};
    assert(cache.read(export_id, 4, 1, &fresh, raw));
    assert(fresh == adapted.exports[4]);
    assert(preparations == 2);
    assert(!cache.read(export_id, exports.size(), 1, nullptr, raw));
    std::cout << "KH0.2 package bounds, edits, and concurrent range cache passed\n";
}
