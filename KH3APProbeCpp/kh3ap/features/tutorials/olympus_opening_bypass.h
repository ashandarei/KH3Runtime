#pragma once

#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace kh3ap
{
    // Audited EGS/Steam he_02 gameflow and camera_ms002; their cooked assets
    // are byte-identical. These are the camp-menu
    // notification after the first VBonus and the wall's camera demonstration,
    // not the existing Free Running tutorial prompt.
    inline auto try_patch_runtime_olympus_opening_bytecode(
            uint8_t* script, size_t size, bool camera) noexcept
            -> RuntimeTutorialPopupBypassResult
    {
        if (!script || size != (camera ? 0x1026u : 0x7116Cu))
            return RuntimeTutorialPopupBypassResult::Rejected;
        const auto bytes = [&](size_t offset, std::initializer_list<uint8_t> expected) {
            return offset <= size && expected.size() <= size - offset &&
                    std::equal(expected.begin(), expected.end(), script + offset);
        };
        const auto jump = [](size_t destination) {
            return std::array<uint8_t, 5>{0x06, uint8_t(destination),
                    uint8_t(destination >> 8), uint8_t(destination >> 16),
                    uint8_t(destination >> 24)};
        };
        const size_t entry = camera ? 0xF66 : 0x1F7DB;
        const auto patched = jump(camera ? 0x580 : 0x1F89C);
        const auto cleanup = jump(0x1F3);
        const size_t cinematic_start_entry = camera ? 0xEF0 : 0x6AFEB;
        const auto cinematic_start = jump(camera ? 0xF1E : 0x9440);
        const auto cinematic_end = jump(0xF8E);
        const bool already = std::equal(patched.begin(), patched.end(), script + entry);
        const bool cleanup_already = camera &&
                std::equal(cleanup.begin(), cleanup.end(), script + 0xFBA);
        const bool cinematic_start_already = std::equal(cinematic_start.begin(),
                cinematic_start.end(), script + cinematic_start_entry);
        const bool cinematic_end_already = camera && std::equal(cinematic_end.begin(),
                cinematic_end.end(), script + 0xF6B);
        if (camera)
        {
            // Skip TresStartCinematicMode before removing the two triggers. Resume
            // where the camera delay normally ends and send the completion event.
            // Skip its paired TresEndCinematicMode, keeping actor visibility.
            // Since no camera was created, go directly to UnloadSelf afterward;
            // do not end a nonexistent camera or force its final aim rotation.
            if ((!already && !bytes(entry, {0x06, 0xC4, 0x05, 0, 0})) ||
                (!cleanup_already && !bytes(0xFBA, {0x06, 0x0F, 0, 0, 0})) ||
                (!cinematic_start_already && !bytes(0xEF0, {0x19, 0x20})) ||
                script[0xF06] != 0x1C || script[0xF1E] != 0x19 ||
                script[0xF34] != 0x1B ||
                !bytes(0x94D, {0x5B, 0x80, 0x05, 0, 0}) ||
                script[0x580] != 0x14 || script[0x5AB] != 0x1C ||
                script[0x5E4] != 0x1C || script[0x1F3] != 0x1B ||
                (!cinematic_end_already && !bytes(0xF6B, {0x19, 0x20})) ||
                script[0xF81] != 0x1C || script[0xF8E] != 0x19 ||
                script[0xFA4] != 0x1C)
                return RuntimeTutorialPopupBypassResult::Rejected;
        }
        else
        {
            // Skip both OpenMessage and its CloseWindow latent call. The window
            // handle is never created; resume at the original cinematic cleanup.
            constexpr char key[] = "system_0010";
            if ((!already && script[entry] != 0x0F) ||
                script[0x1F754] != 0x1F ||
                std::memcmp(script + 0x1F755, key, sizeof(key)) != 0 ||
                script[0x1F803] != 0x1C ||
                !bytes(0x1F81A, {0x5B, 0x4B, 0xF8, 0x01, 0}) ||
                !bytes(0x1F84B, {0x19, 0x20}) || script[0x1F861] != 0x1C ||
                !bytes(0x1F881, {0x5B, 0x9C, 0xF8, 0x01, 0}) ||
                !bytes(0x1F89C, {0x19, 0x20}) || script[0x1F8B2] != 0x1C ||
                script[0x1F8BF] != 0x1B ||
                // After setting HE_OLYMPUS_RTEV1_2, dispatch the wall start event
                // directly. The intervening cinematic setup also stops Sora,
                // independently of the camera script's own cinematic call.
                (!cinematic_start_already &&
                        !bytes(0x6AFEB, {0x07, 0xA2, 0xB1, 0x06, 0})) ||
                script[0x6AFF0] != 0x27 ||
                !bytes(0x6AFB1, {0x19, 0x20}) || script[0x6AFC7] != 0x1C ||
                !bytes(0x9440, {0x14, 0x00}) ||
                !bytes(0x944A, {0x19, 0x20}) || script[0x9460] != 0x1C ||
                script[0x946A] != 0x21 || script[0x9478] != 0x4D)
                return RuntimeTutorialPopupBypassResult::Rejected;
        }
        if (already && cinematic_start_already &&
                (!camera || (cleanup_already && cinematic_end_already)))
            return RuntimeTutorialPopupBypassResult::AlreadyPatched;
        std::copy(patched.begin(), patched.end(), script + entry);
        std::copy(cinematic_start.begin(), cinematic_start.end(),
                script + cinematic_start_entry);
        if (camera)
        {
            std::copy(cinematic_end.begin(), cinematic_end.end(), script + 0xF6B);
            std::copy(cleanup.begin(), cleanup.end(), script + 0xFBA);
        }
        return RuntimeTutorialPopupBypassResult::Patched;
    }
}
