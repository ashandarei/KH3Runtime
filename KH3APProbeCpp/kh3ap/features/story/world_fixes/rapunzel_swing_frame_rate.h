#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    inline constexpr auto runtime_rapunzel_swing_camera_class = L"ra_01_camera_ms070_C";
    inline constexpr size_t rapunzel_swing_camera_script_size = 20331;
    enum class RapunzelSwingFrameRateResult { rejected, applied, already_applied };

    struct RapunzelSwingFrameRateCall
    {
        size_t offset;
        bool enables_cap;
    };

    // Reviewed EGS camera ubergraph: three crossing starts enable the event
    // cutscene FPS cap; six finish/skip paths disable it. Leave cleanup intact.
    inline constexpr std::array<RapunzelSwingFrameRateCall, 9> rapunzel_swing_fps_calls{{
        {6429, false}, {6950, true}, {7127, true}, {7315, true},
        {7755, false}, {7846, false}, {12927, false}, {15246, false}, {18141, false},
    }};

    inline auto patch_rapunzel_swing_frame_rate(
            uint8_t* script, size_t size, uintptr_t fps_function)
            -> RapunzelSwingFrameRateResult
    {
        static_assert(sizeof(uintptr_t) == 8);
        if (!script || size != rapunzel_swing_camera_script_size || !fps_function)
            return RapunzelSwingFrameRateResult::rejected;

        bool all_patched = true;
        for (const auto& call : rapunzel_swing_fps_calls)
        {
            // Linked runtime bytecode: EX_FinalFunction, UFunction*, EX_Self,
            // EX_True/EX_False, EX_EndFunctionParms. Validate every call before
            // writing anything, including the untouched finish/skip calls.
            const auto* bytes = script + call.offset;
            uintptr_t target{};
            std::memcpy(&target, bytes + 1, sizeof(target));
            if (bytes[0] != 0x1C || target != fps_function ||
                bytes[9] != 0x17 || bytes[11] != 0x16 ||
                (bytes[10] != 0x28 && (!call.enables_cap || bytes[10] != 0x27)))
                return RapunzelSwingFrameRateResult::rejected;
            if (call.enables_cap) all_patched &= bytes[10] == 0x28;
        }
        if (all_patched) return RapunzelSwingFrameRateResult::already_applied;
        for (const auto& call : rapunzel_swing_fps_calls)
            if (call.enables_cap) script[call.offset + 10] = 0x28; // EX_False
        return RapunzelSwingFrameRateResult::applied;
    }

    auto try_apply_rapunzel_swing_frame_rate(RC::Unreal::UObject* object) -> bool;
}
