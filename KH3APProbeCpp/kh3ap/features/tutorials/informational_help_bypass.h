#pragma once

#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"
#include "kh3ap/features/tutorials/olympus_opening_bypass.h"
#include "kh3ap/features/tutorials/toy_box_cactuar_bypass.h"
#include <array>
#include <cstring>
#include <string_view>

namespace kh3ap
{
    inline constexpr std::array<std::wstring_view, 6> runtime_informational_help_class_names{
            L"ra_01_gameflow_C", L"fz_03_gameflow_C",
            L"he_02_gameflow_C", L"he_02_camera_ms002_C",
            L"ts_02_camera_ms141_C", L"ts_02_camera_ms142_C"};
    inline constexpr std::array<std::wstring_view, 6> runtime_informational_help_function_paths{
            L"/Game/Levels/ra/ra_01/umap/ra_01_gameflow.ra_01_gameflow_C:ExecuteUbergraph_ra_01_gameflow",
            L"/Game/Levels/fz/fz_03/umap/fz_03_gameflow.fz_03_gameflow_C:ExecuteUbergraph_fz_03_gameflow",
            L"/Game/Levels/he/he_02/umap/he_02_gameflow.he_02_gameflow_C:ExecuteUbergraph_he_02_gameflow",
            L"/Game/Levels/he/he_02/umap/he_02_camera_ms002.he_02_camera_ms002_C:ExecuteUbergraph_he_02_camera_ms002",
            L"/Game/Levels/ts/ts_02/umap/ts_02_camera_ms141.ts_02_camera_ms141_C:ExecuteUbergraph_ts_02_camera_ms141",
            L"/Game/Levels/ts/ts_02/umap/ts_02_camera_ms142.ts_02_camera_ms142_C:ExecuteUbergraph_ts_02_camera_ms142"};
    inline constexpr std::array<std::wstring_view, 6> runtime_informational_help_context_prefixes{
            L"/Game/Levels/ra/ra_01/", L"/Game/Levels/fz/fz_03/",
            L"/Game/Levels/he/he_02/", L"/Game/Levels/he/he_02/",
            L"/Game/Levels/ts/ts_02/", L"/Game/Levels/ts/ts_02/"};

    // EGS/Steam asset audit: bypass only TresUI_OpenGameHelp, entering the exact
    // latent post-dismiss continuation. Fades, cinematic cleanup, grants,
    // map sets and story logic on either side remain byte-for-byte intact.
    constexpr auto informational_help_profile(size_t size, size_t call,
            size_t resume, size_t false_target, uint8_t id)
            -> RuntimeTutorialPopupBypassProfile
    {
        return {size, call, 0x20, call, call + 0x16, call + 0x2D,
                resume, resume, 0x07, resume, 0x07, call + 0x46, id,
                0, {}, true, false_target};
    }
    inline constexpr std::array<RuntimeTutorialPopupBypassProfile, 4>
            runtime_informational_help_profiles{{
                    informational_help_profile(0x9A222, 0x2E424, 0x2E46F, 0x2E510, 37),
                    informational_help_profile(0x9A222, 0x95E72, 0x3487F, 0x35158, 27),
                    informational_help_profile(0x9A222, 0x99242, 0x396D2, 0x3ADA1, 32),
                    informational_help_profile(0x14D37, 0x120B4, 0x6D0F, 0x6DB0, 28),
            }};

    // Steam Corona has a different layout; Arendelle is identical. Select by
    // exact script size, then validate every call/continuation independently.
    inline constexpr std::array<RuntimeTutorialPopupBypassProfile, 4>
            runtime_informational_help_steam_profiles{{
                    informational_help_profile(0x9A2D8, 0x2E4B7, 0x2E502, 0x2E5A3, 37),
                    informational_help_profile(0x9A2D8, 0x95F28, 0x34912, 0x351EB, 27),
                    informational_help_profile(0x9A2D8, 0x992F8, 0x39765, 0x3AE34, 32),
                    runtime_informational_help_profiles[3],
            }};

    [[nodiscard]] inline auto try_patch_runtime_informational_help_bytecode(
            uint8_t* script, size_t size, size_t variant) noexcept
            -> RuntimeTutorialPopupBypassResult
    {
        if (variant >= runtime_informational_help_class_names.size())
            return RuntimeTutorialPopupBypassResult::Rejected;
        if (variant >= 4)
            return try_patch_runtime_toy_box_cactuar_bytecode(script, size, variant == 5);
        if (variant >= 2)
            return try_patch_runtime_olympus_opening_bytecode(script, size, variant == 3);
        const auto& profiles = variant == 0 &&
                size == runtime_informational_help_steam_profiles[0].script_size
                ? runtime_informational_help_steam_profiles : runtime_informational_help_profiles;
        bool changed = false;
        const size_t first = variant == 0 ? 0 : 3;
        const size_t end = variant == 0 ? 3 : 4;
        if (!script || size != profiles[first].script_size)
            return RuntimeTutorialPopupBypassResult::Rejected;
        std::array<std::array<uint8_t, 5>, 4> originals{};
        for (size_t i = first; i < end; ++i)
            std::memcpy(originals[i].data(), script +
                    profiles[i].bypass_entry_offset, 5);
        for (size_t i = first; i < end; ++i)
        {
            const auto result = try_patch_runtime_tutorial_popup_bytecode(
                    script, size, profiles[i]);
            if (result == RuntimeTutorialPopupBypassResult::Rejected)
            {
                // A changed third panel must not leave the first two patched.
                for (size_t j = first; j < i; ++j)
                    std::memcpy(script + profiles[j].bypass_entry_offset,
                            originals[j].data(), 5);
                return result;
            }
            changed |= result == RuntimeTutorialPopupBypassResult::Patched;
        }
        return changed ? RuntimeTutorialPopupBypassResult::Patched
                       : RuntimeTutorialPopupBypassResult::AlreadyPatched;
    }
}
