#include "kh3ap/features/story/world_fixes/olympus_intro_camera.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    using namespace kh3ap;
    // Optional passive runtime captures validate the real linked script layout
    // without distributing game bytecode as a repository fixture.
    if (argc == 3)
    {
        const auto read = [](const char* path) {
            std::ifstream stream(path, std::ios::binary);
            assert(stream);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
        };
        auto entry = read(argv[1]);
        const auto graph = read(argv[2]);
        assert(entry.size() == 22 && graph.size() == 1151);
        OlympusIntroScriptName graph_name{};
        OlympusIntroScriptName completion_name{};
        std::memcpy(graph_name.data(), entry.data() + 1, sizeof(graph_name));
        std::memcpy(completion_name.data(), graph.data() + 1030, sizeof(completion_name));
        auto expected = entry;
        const uint32_t destination = 987;
        std::memcpy(expected.data() + 14, &destination, sizeof(destination));
        assert(patch_olympus_intro_camera(entry.data(), entry.size(), graph.data(), graph.size(),
                                          graph_name, completion_name) != OlympusIntroCameraResult::rejected);
        assert(entry == expected);
    }
    constexpr OlympusIntroScriptName graph_name{123, 123, 0};
    constexpr OlympusIntroScriptName completion_name{456, 456, 0};
    std::array<uint8_t, 22> original{};
    original[0] = 0x1B;
    std::memcpy(original.data() + 1, graph_name.data(), sizeof(graph_name));
    original[13] = 0x1D;
    const uint32_t play = 1044;
    std::memcpy(original.data() + 14, &play, sizeof(play));
    original[18] = 0x16;
    original[19] = 0x04;
    original[20] = 0x0B;
    original[21] = 0x53;
    std::vector<uint8_t> graph(1151, 0);
    auto* callback = graph.data() + 987;
    callback[0] = 0x14;
    callback[1] = 0x00;
    callback[10] = 0x19;
    callback[11] = 0x20;
    callback[20] = 24;
    callback[32] = 0x1C;
    callback[41] = 0x17;
    callback[42] = 0x21;
    std::memcpy(callback + 43, completion_name.data(), sizeof(completion_name));
    callback[55] = 0x16;
    callback[56] = 0x4D;
    graph[1044] = 0x14;
    const auto original_graph = graph;
    auto entry = original;
    const auto apply = [&]() {
        return patch_olympus_intro_camera(entry.data(), entry.size(), graph.data(), graph.size(),
                                          graph_name, completion_name);
    };
    auto expected = original;
    const uint32_t completion = 987;
    std::memcpy(expected.data() + 14, &completion, sizeof(completion));
    assert(apply() == OlympusIntroCameraResult::applied);
    assert(entry == expected && graph == original_graph);
    assert(apply() == OlympusIntroCameraResult::already_applied);

    // Reject unexpected entry signatures and other event destinations without
    // writing anything, including when a previously patched entry is rechecked.
    for (const auto offset : {0, 1, 13, 14, 18, 19, 20, 21})
    {
        entry = original;
        entry[offset] = 0xFF;
        const auto before = entry;
        assert(apply() == OlympusIntroCameraResult::rejected);
        assert(entry == before);
    }
    for (const auto offset : {987, 988, 997, 998, 1007, 1019, 1028, 1029, 1030, 1042, 1043, 1044})
    {
        graph = original_graph;
        graph[offset] = 0xFF;
        for (const auto baseline : {original, expected})
        {
            entry = baseline;
            assert(apply() == OlympusIntroCameraResult::rejected);
            assert(entry == baseline);
        }
    }
    entry = original;
    graph = original_graph;
    assert(patch_olympus_intro_camera(nullptr, 22, graph.data(), graph.size(), graph_name,
                                      completion_name) == OlympusIntroCameraResult::rejected);
    graph.pop_back();
    assert(apply() == OlympusIntroCameraResult::rejected);
    assert(entry == original);
}
