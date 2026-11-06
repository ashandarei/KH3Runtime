#include "kh3ap/features/story/world_fixes/rapunzel_water_bypass.h"
#include <cassert>
#include <vector>

int main()
{
    using namespace kh3ap;
    std::vector<uint8_t> original(129000, 0x53);
    for (const auto& entry : rapunzel_water_entries)
    {
        original[entry[0]] = 0x06;
        std::memcpy(original.data() + entry[0] + 1, &entry[1], 4);
    }
    auto script = original;
    assert(patch_rapunzel_water_bytecode(script.data(), script.size()) == RapunzelWaterPatchResult::applied);
    auto expected = original;
    for (const auto& entry : rapunzel_water_entries)
    {
        expected[entry[0]] = 0x04;
        expected[entry[0] + 1] = 0x0B;
    }
    assert(script == expected); // No other interaction or bytecode offset changes.
    assert(patch_rapunzel_water_bytecode(script.data(), script.size()) == RapunzelWaterPatchResult::already_applied);
    for (const auto& entry : rapunzel_water_entries)
    {
        auto invalid = original;
        invalid[entry[0] + 3] ^= 1;
        const auto before = invalid;
        assert(patch_rapunzel_water_bytecode(invalid.data(), invalid.size()) == RapunzelWaterPatchResult::rejected);
        assert(invalid == before); // Reject atomically even if the other site matches.
    }
    script = original;
    assert(patch_rapunzel_water_bytecode(script.data(), 128572) == RapunzelWaterPatchResult::rejected);
    assert(script == original);
    assert(patch_rapunzel_water_bytecode(nullptr, 129000) == RapunzelWaterPatchResult::rejected);
}
