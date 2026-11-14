#include "kh3ap/features/story/world_fixes/olympus_collapse_camera.h"
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
    OlympusCollapseCameraLinks links{0x12345678, 0x23456789, 0x34567890,
        {{{11, 11, 0}, {22, 22, 0}, {33, 33, 0}}}, {44, 44, 0}};
    std::vector<uint8_t> original(11501, 0);
    const auto write = [&](size_t offset, const auto& value) {
        std::memcpy(original.data() + offset, &value, sizeof(value));
    };
    constexpr std::array<size_t, 3> spawn{2161, 2974, 4458};
    for (size_t i = 0; i < spawn.size(); ++i)
    {
        const auto s = spawn[i];
        original[s] = 0x5F; original[s + 1] = 0x00;
        original[s + 10] = 0x19; original[s + 11] = 0x20;
        write(s + 20, uint32_t{33}); original[s + 32] = 0x1C;
        write(s + 33, links.spawn); original[s + 41] = 0x17;
        original[s + 42] = 0x20; original[s + 51] = 0x21;
        write(s + 52, links.camera_names[i]); original[s + 64] = 0x16;
    }
    for (const auto s : {4228, 6186})
    {
        original[s] = 0x19; original[s + 1] = 0x20;
        write(s + 10, uint32_t{21}); original[s + 22] = 0x1C;
        write(s + 23, s == 4228 ? links.end_same : links.end_all);
        original[s + 31] = 0x17; original[s + 32] = 0x28;
        original[s + 33] = 0x00; original[s + 42] = 0x16;
    }
    original[11437] = 0x19; original[11438] = 0x20;
    write(11447, uint32_t{28}); original[11459] = 0x1B;
    write(11460, links.end_normal); original[11486] = 0x16;
    original[2897] = 0x19; original[5231] = 0x19;
    original[4271] = 0x14; original[6229] = 0x14;
    original[11487] = 0x4D; original[11500] = 0x53;

    // Optional locally captured retail graph, never distributed in the repo.
    if (argc == 2)
    {
        std::ifstream stream(argv[1], std::ios::binary);
        assert(stream);
        original.assign(std::istreambuf_iterator<char>(stream), {});
        assert(original.size() == 11501);
        std::memcpy(&links.spawn, original.data() + 2194, sizeof(links.spawn));
        std::memcpy(&links.end_same, original.data() + 4251, sizeof(links.end_same));
        std::memcpy(&links.end_all, original.data() + 6209, sizeof(links.end_all));
        for (size_t i = 0; i < spawn.size(); ++i)
            std::memcpy(&links.camera_names[i], original.data() + spawn[i] + 52, sizeof(OlympusCollapseName));
        std::memcpy(&links.end_normal, original.data() + 11460, sizeof(OlympusCollapseName));
    }
    auto script = original;
    const auto apply = [&] { return patch_olympus_collapse_camera(script.data(), script.size(), links); };
    assert(apply() != OlympusCollapseCameraResult::rejected);
    const auto patched = script;
    assert(apply() == OlympusCollapseCameraResult::already_applied && script == patched);
    const std::array<size_t, 6> starts{2161, 2974, 4458, 4228, 6186, 11437};
    const std::array<uint32_t, 6> ends{2897, 2897, 5231, 4271, 6229, 11487};
    auto expected = original;
    for (size_t i = 0; i < starts.size(); ++i)
    {
        expected[starts[i]] = 0x06;
        std::memcpy(expected.data() + starts[i] + 1, &ends[i], sizeof(ends[i]));
    }
    // No other byte may change, including rubble Play calls and RemoteEvent.
    assert(patched == expected);
    for (const auto offset : {2161, 2171, 2181, 2193, 2194, 2213, 2225,
                             2974, 3007, 3026, 4458, 4491, 4510, 4228,
                             4251, 4270, 6186, 6209, 6228, 11437, 11447,
                             11459, 11460, 11486, 2897, 5231, 4271, 6229, 11487, 11500})
    {
        for (const auto& baseline : {original, patched})
        {
            script = baseline;
            script[offset] ^= 0xFF;
            const auto before = script;
            assert(apply() == OlympusCollapseCameraResult::rejected);
            assert(script == before);
        }
    }
    script = original;
    script.pop_back();
    const auto before = script;
    assert(apply() == OlympusCollapseCameraResult::rejected && script == before);
    assert(patch_olympus_collapse_camera(nullptr, 11501, links) == OlympusCollapseCameraResult::rejected);
}
