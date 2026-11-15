#include "kh3ap/features/story/world_fixes/olympus_collapse_camera.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

using namespace kh3ap;

int main(int argc, char** argv)
{
    const auto read = [](const char* path) {
        std::ifstream file(path, std::ios::binary);
        assert(file);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    };
    OlympusCollapseMovementLinks links{0x12345678, 0x23456789, 0x34567890};
    std::vector<uint8_t> original(11501, 0);
    const auto write = [&](size_t offset, const auto& value) {
        std::memcpy(original.data() + offset, &value, sizeof(value));
    };
    const std::array<uint32_t, 7> starts{581, 7772, 8112, 10035, 4282, 6039, 1485};
    const std::array<uint32_t, 7> ends{627, 7818, 8158, 10081, 4317, 6074, 1748};
    for (size_t i = 0; i < 6; ++i)
    {
        const auto s = starts[i];
        original[s] = 0x19; original[s + 1] = 0x20;
        write(s + 10, uint32_t{i < 4 ? 24u : 13u});
        original[s + 22] = 0x1C;
        write(s + 23, i < 4 ? links.start_cinematic : links.end_cinematic);
        original[s + 31] = 0x17; original[ends[i] - 1] = 0x16;
    }
    original[1485] = 0x5F; original[1495] = 0x19; original[1496] = 0x20;
    write(1505, uint32_t{11}); original[1517] = 0x1C; write(1518, links.get_pawn);
    original[1526] = 0x17; original[1527] = 0x16;
    original[1748] = 0x4C; write(1749, uint32_t{3806});
    original[1753] = 0x4C; write(1754, uint32_t{3861});
    original[627] = 0x4D; original[7818] = 0x06; original[8158] = 0x06;
    original[10081] = 0x4C; original[4317] = 0x19; original[6074] = 0x19;
    if (argc == 4)
    {
        original = read(argv[1]);
        assert(original.size() == 11501);
        std::memcpy(&links.start_cinematic, original.data() + 604, sizeof(uintptr_t));
        std::memcpy(&links.end_cinematic, original.data() + 4305, sizeof(uintptr_t));
        std::memcpy(&links.get_pawn, original.data() + 1518, sizeof(uintptr_t));
    }
    auto script = original;
    const auto apply = [&] { return patch_olympus_collapse_movement(script.data(), script.size(), links); };
    assert(apply() != OlympusCollapseCameraResult::rejected);
    const auto patched = script;
    auto expected = original;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        expected[starts[i]] = 0x06;
        std::memcpy(expected.data() + starts[i] + 1, &ends[i], sizeof(ends[i]));
    }
    assert(patched == expected);
    assert(apply() == OlympusCollapseCameraResult::already_applied && script == patched);
    for (const auto s : starts)
    {
        script = original;
        script[s] = 0xFF;
        const auto before = script;
        assert(apply() == OlympusCollapseCameraResult::rejected && script == before);
    }
    for (const auto offset : {591, 603, 604, 612, 626, 7795, 8135, 10058, 4305, 6062,
                             1505, 1517, 1518, 1526, 1749, 1754, 627, 7818, 8158, 10081, 4317, 6074})
    {
        for (const auto& baseline : {original, patched})
        {
            script = baseline;
            script[offset] ^= 0xFF;
            const auto before = script;
            assert(apply() == OlympusCollapseCameraResult::rejected && script == before);
        }
    }
    script = original;
    script.resize(11500);
    const auto truncated = script;
    assert(apply() == OlympusCollapseCameraResult::rejected && script == truncated);

    OlympusCollapseName graph_name{111, 111, 0}, voice_name{222, 222, 0};
    std::vector<uint8_t> entry(22, 0), graph(464063, 0);
    entry[0] = 0x1B; entry[13] = 0x1D; entry[18] = 0x16;
    entry[19] = 0x04; entry[20] = 0x0B; entry[21] = 0x53;
    std::memcpy(entry.data() + 1, &graph_name, sizeof(graph_name));
    const uint32_t old_entry = 463011;
    std::memcpy(entry.data() + 14, &old_entry, sizeof(old_entry));
    graph[463011] = 0x06;
    auto* callback = graph.data() + 19103;
    callback[0] = 0x14; callback[1] = 0x00; callback[10] = 0x19; callback[11] = 0x20;
    callback[20] = 24; callback[32] = 0x1C; callback[41] = 0x17; callback[42] = 0x21;
    std::memcpy(callback + 43, &voice_name, sizeof(voice_name));
    callback[55] = 0x16; callback[56] = 0x4D;
    if (argc == 4)
    {
        entry = read(argv[2]); graph = read(argv[3]);
        assert(entry.size() == 22 && graph.size() == 464063);
        std::memcpy(&graph_name, entry.data() + 1, sizeof(graph_name));
        std::memcpy(&voice_name, graph.data() + 19146, sizeof(voice_name));
    }
    const auto original_entry = entry;
    const auto original_graph = graph;
    const auto complete = [&] { return patch_olympus_collapse_completion(entry.data(), entry.size(),
            graph.data(), graph.size(), graph_name, voice_name); };
    assert(complete() != OlympusCollapseCameraResult::rejected);
    auto expected_entry = original_entry;
    const uint32_t resume = 19103;
    std::memcpy(expected_entry.data() + 14, &resume, sizeof(resume));
    assert(entry == expected_entry && graph == original_graph);
    assert(complete() == OlympusCollapseCameraResult::already_applied);
    for (const auto offset : {0, 1, 13, 14, 18, 19, 20, 21})
    {
        entry = original_entry; entry[offset] ^= 0xFF;
        const auto before = entry;
        assert(complete() == OlympusCollapseCameraResult::rejected && entry == before);
    }
    entry = original_entry;
    for (const auto offset : {19103, 19104, 19113, 19114, 19123, 19135, 19144, 19145, 19146, 19158, 19159, 463011})
    {
        graph = original_graph; graph[offset] ^= 0xFF;
        assert(complete() == OlympusCollapseCameraResult::rejected && entry == original_entry);
    }
}
