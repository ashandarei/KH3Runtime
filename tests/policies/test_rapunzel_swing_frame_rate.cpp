#include "kh3ap/features/story/world_fixes/rapunzel_swing_frame_rate.h"
#include <cassert>
#include <vector>

int main()
{
    using namespace kh3ap;
    constexpr uintptr_t function = 0x123456789ABC;
    std::vector<uint8_t> original(rapunzel_swing_camera_script_size, 0x53);
    for (const auto& call : rapunzel_swing_fps_calls)
    {
        auto* bytes = original.data() + call.offset;
        bytes[0] = 0x1C;
        std::memcpy(bytes + 1, &function, sizeof(function));
        bytes[9] = 0x17;
        bytes[10] = call.enables_cap ? 0x27 : 0x28;
        bytes[11] = 0x16;
    }
    auto script = original;
    assert(patch_rapunzel_swing_frame_rate(script.data(), script.size(), function) ==
           RapunzelSwingFrameRateResult::applied);
    auto expected = original;
    size_t changed = 0;
    for (const auto& call : rapunzel_swing_fps_calls)
        if (call.enables_cap) { expected[call.offset + 10] = 0x28; ++changed; }
    assert(changed == 3);
    assert(script == expected); // Timing, animations and all cleanup bytes survive.
    assert(patch_rapunzel_swing_frame_rate(script.data(), script.size(), function) ==
           RapunzelSwingFrameRateResult::already_applied);

    for (const auto& call : rapunzel_swing_fps_calls)
        for (const size_t index : {0, 1, 9, 10, 11})
        {
            auto invalid = original;
            invalid[call.offset + index] = index == 10 ? 0x0B : 0xFF;
            const auto before = invalid;
            assert(patch_rapunzel_swing_frame_rate(invalid.data(), invalid.size(), function) ==
                   RapunzelSwingFrameRateResult::rejected);
            assert(invalid == before); // No partial patch when a later call differs.
        }
    script = original;
    script[rapunzel_swing_fps_calls[0].offset + 10] = 0x27;
    const auto invalid_cleanup = script;
    assert(patch_rapunzel_swing_frame_rate(script.data(), script.size(), function) ==
           RapunzelSwingFrameRateResult::rejected);
    assert(script == invalid_cleanup);

    script = original;
    script[rapunzel_swing_fps_calls[1].offset + 10] = 0x28;
    assert(patch_rapunzel_swing_frame_rate(script.data(), script.size(), function) ==
           RapunzelSwingFrameRateResult::applied);
    assert(script == expected);
    for (const size_t size : {size_t{0}, size_t{7326}, original.size() - 1, original.size() + 1})
    {
        script = original;
        assert(patch_rapunzel_swing_frame_rate(script.data(), size, function) ==
               RapunzelSwingFrameRateResult::rejected);
        assert(script == original);
    }
    assert(patch_rapunzel_swing_frame_rate(nullptr, original.size(), function) ==
           RapunzelSwingFrameRateResult::rejected);
    assert(patch_rapunzel_swing_frame_rate(original.data(), original.size(), 0) ==
           RapunzelSwingFrameRateResult::rejected);
    assert(patch_rapunzel_swing_frame_rate(original.data(), original.size(), function + 1) ==
           RapunzelSwingFrameRateResult::rejected);
}
