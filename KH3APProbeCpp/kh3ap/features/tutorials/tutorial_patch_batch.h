#pragma once

#include "tutorial_popup_bypass.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kh3ap::tutorial_patch
{
    using TutorialPatcher = RuntimeTutorialPopupBypassResult (*)(uint8_t*, size_t) noexcept;

    template <size_t PatchCount>
    [[nodiscard]] auto apply_tutorial_patch_batch(
            uint8_t* script,
            size_t script_size,
            const std::array<TutorialPatcher, PatchCount>& patchers) noexcept
            -> std::array<RuntimeTutorialPopupBypassResult, PatchCount>
    {
        std::array<RuntimeTutorialPopupBypassResult, PatchCount> results{};
        for (size_t index = 0; index < PatchCount; ++index)
        {
            // Each patch validates its own callsite. Preserve the complete
            // patch sequence even when an earlier callsite is rejected.
            results[index] = patchers[index](script, script_size);
        }
        return results;
    }

    template <size_t PatchCount>
    [[nodiscard]] auto combine_tutorial_patch_results(
            const std::array<RuntimeTutorialPopupBypassResult, PatchCount>& results) noexcept
            -> RuntimeTutorialPopupBypassResult
    {
        auto combined = RuntimeTutorialPopupBypassResult::AlreadyPatched;
        for (const auto result : results)
        {
            if (result == RuntimeTutorialPopupBypassResult::Rejected)
            {
                return result;
            }
            if (result == RuntimeTutorialPopupBypassResult::Patched)
            {
                combined = result;
            }
        }
        return combined;
    }
}
