#include "kh3ap/features/story/world_fixes/rapunzel_hair_approach.h"
#include <cassert>
#include <vector>

int main()
{
    using namespace kh3ap;
    constexpr uintptr_t camera_property = 0x123456789ABC;
    constexpr RapunzelApproachScriptName run_name{123, 123, 0};
    std::vector<uint8_t> original(rapunzel_hair_approach_script_size, 0x53);
    auto* gate = original.data() + rapunzel_hair_approach_camera_gate;
    gate[0] = 0x07;
    std::memcpy(gate + 1, &rapunzel_hair_approach_camera_resume, sizeof(uint32_t));
    gate[5] = 0x01;
    std::memcpy(gate + 6, &camera_property, sizeof(camera_property));
    for (const auto offset : {rapunzel_hair_approach_run_start, rapunzel_hair_approach_run_end})
    {
        auto* call = original.data() + offset;
        call[0] = 0x1B;
        std::memcpy(call + 1, run_name.data(), sizeof(run_name));
        call[13] = offset == rapunzel_hair_approach_run_start ? 0x27 : 0x28;
        call[14] = 0x16;
    }
    auto expected = original;
    expected[rapunzel_hair_approach_camera_gate] = 0x06;
    expected[rapunzel_hair_approach_run_start + 13] = 0x28;
    auto script = original;
    assert(patch_rapunzel_hair_approach(script.data(), script.size(), camera_property, run_name) ==
           RapunzelHairApproachResult::applied);
    assert(script == expected); // Only two bytes change; swing and cleanup survive.
    assert(patch_rapunzel_hair_approach(script.data(), script.size(), camera_property, run_name) ==
           RapunzelHairApproachResult::already_applied);
    for (const auto offset : {rapunzel_hair_approach_camera_gate,
                              rapunzel_hair_approach_camera_gate + 1,
                              rapunzel_hair_approach_camera_gate + 5,
                              rapunzel_hair_approach_camera_gate + 6,
                              rapunzel_hair_approach_run_start,
                              rapunzel_hair_approach_run_start + 1,
                              rapunzel_hair_approach_run_start + 13,
                              rapunzel_hair_approach_run_start + 14,
                              rapunzel_hair_approach_run_end,
                              rapunzel_hair_approach_run_end + 1,
                              rapunzel_hair_approach_run_end + 13,
                              rapunzel_hair_approach_run_end + 14})
    {
        auto invalid = original;
        invalid[offset] = 0xFF;
        const auto before = invalid;
        assert(patch_rapunzel_hair_approach(invalid.data(), invalid.size(), camera_property, run_name) ==
               RapunzelHairApproachResult::rejected);
        assert(invalid == before); // A late mismatch must not partially patch the camera.
    }
    for (const auto offset : {rapunzel_hair_approach_camera_gate,
                              rapunzel_hair_approach_run_start + 13})
    {
        script = original;
        script[offset] = expected[offset];
        assert(patch_rapunzel_hair_approach(script.data(), script.size(), camera_property, run_name) ==
               RapunzelHairApproachResult::applied);
        assert(script == expected);
    }
    for (const size_t size : {size_t{0}, original.size() - 1, original.size() + 1})
    {
        script = original;
        assert(patch_rapunzel_hair_approach(script.data(), size, camera_property, run_name) ==
               RapunzelHairApproachResult::rejected);
        assert(script == original);
    }
    assert(patch_rapunzel_hair_approach(nullptr, original.size(), camera_property, run_name) ==
           RapunzelHairApproachResult::rejected);
    assert(patch_rapunzel_hair_approach(original.data(), original.size(), 0, run_name) ==
           RapunzelHairApproachResult::rejected);
    assert(patch_rapunzel_hair_approach(original.data(), original.size(), camera_property + 1, run_name) ==
           RapunzelHairApproachResult::rejected);
    assert(patch_rapunzel_hair_approach(original.data(), original.size(), camera_property, {124, 123, 0}) ==
           RapunzelHairApproachResult::rejected);
}
