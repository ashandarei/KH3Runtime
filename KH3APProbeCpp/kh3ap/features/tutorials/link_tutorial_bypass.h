#pragma once

#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"

#include <array>
#include <cstring>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeLinkTutorialVariant : size_t { Simba, Stitch, Pixel, Count };
    inline constexpr size_t runtime_link_tutorial_count =
            static_cast<size_t>(RuntimeLinkTutorialVariant::Count);
    inline constexpr std::array<std::wstring_view, runtime_link_tutorial_count>
            runtime_link_tutorial_class_names{L"mi_03_gameflow_C", L"bx_02_gameflow_C", L"ts_03_gameflow_C"};
    inline constexpr std::array<std::wstring_view, runtime_link_tutorial_count>
            runtime_link_tutorial_function_paths{
                    L"/Game/Levels/mi/mi_03/umap/mi_03_gameflow.mi_03_gameflow_C:ExecuteUbergraph_mi_03_gameflow",
                    L"/Game/Levels/bx/bx_02/umap/bx_02_gameflow.bx_02_gameflow_C:ExecuteUbergraph_bx_02_gameflow",
                    L"/Game/Levels/ts/ts_03/umap/ts_03_gameflow.ts_03_gameflow_C:ExecuteUbergraph_ts_03_gameflow"};
    inline constexpr std::array<std::wstring_view, runtime_link_tutorial_count>
            runtime_link_tutorial_context_prefixes{
                    L"/Game/Levels/mi/mi_03/", L"/Game/Levels/bx/bx_02/", L"/Game/Levels/ts/ts_03/"};

    // Extracted EGS gameflow bytecode, using in-memory (64-bit pointer) offsets.
    // Redirect only the local WaitBPTresGetKeyitem and help calls to the help's
    // existing completion continuation. Keep all story flags, map cleanup and
    // shared grant functions intact: AP receipts still call BP_TresGetItem.
    struct RuntimeLinkTutorialProfile
    {
        size_t script_size;
        size_t conversion_call;
        size_t item_id;
        uint8_t item;
        size_t grant;
        uint32_t grant_resume;
        size_t help;
        uint8_t help_id;
        uint32_t resume;
        uint32_t resume_false_target;
    };
    inline constexpr std::array<RuntimeLinkTutorialProfile, runtime_link_tutorial_count>
            runtime_link_tutorial_profiles{{
                    {0x3CC76, 0x18329, 0x18333, 9, 0x18336, 0x18387,
                     0x1838D, 41, 0x17B56, 0x17DE6},
                    {0xA6175, 0x419A7, 0x419B1, 8, 0x419B4, 0x41A05,
                     0x41A58, 42, 0x2FD6C, 0x2FE1E},
                    // Beat of Lead's first story clear grants KEY_ITEM07
                    // directly before opening 8-Bit Blast help. Resume at
                    // the existing post-help fade/reset/map-return block.
                    {0x13DB0, 0xC1AE, 0xC1B8, 7, 0xC1BB, 0xC102,
                     0xC108, 40, 0x7FFA, 0x8116},
            }};

    [[nodiscard]] inline auto try_patch_runtime_link_tutorial_bytecode(
            uint8_t* script, size_t size, RuntimeLinkTutorialVariant variant) noexcept
            -> RuntimeTutorialPopupBypassResult
    {
        const auto index = static_cast<size_t>(variant);
        if (!script || index >= runtime_link_tutorial_count)
            return RuntimeTutorialPopupBypassResult::Rejected;
        const auto& p = runtime_link_tutorial_profiles[index];
        if (size != p.script_size)
            return RuntimeTutorialPopupBypassResult::Rejected;

        const auto offset_is = [script](size_t at, uint8_t opcode, uint32_t target) {
            uint32_t value{};
            std::memcpy(&value, script + at + 1, sizeof(value));
            return script[at] == opcode && value == target;
        };
        const auto original_call = [script](size_t at) {
            return script[at] == 0x19 && script[at + 1] == 0x20;
        };
        const bool grant_patched = offset_is(p.grant, 0x06, p.resume);
        const bool help_patched = offset_is(p.help, 0x06, p.resume);

        // Validate BOTH calls before writing either, including the literal
        // heartbinder ID and both latent continuations. Relocated pointers are
        // intentionally not matched. Unknown asset revisions fail closed.
        if ((!grant_patched && !original_call(p.grant)) ||
            (!help_patched && !original_call(p.help)) ||
            script[p.conversion_call] != 0x1C ||
            script[p.item_id] != 0x24 || script[p.item_id + 1] != p.item ||
            script[p.grant + 0x16] != 0x1C ||
            !offset_is(p.grant + 0x2D, 0x5B, p.grant_resume) ||
            script[p.help + 0x16] != 0x1C ||
            !offset_is(p.help + 0x2D, 0x5B, p.resume) ||
            script[p.help + 0x46] != 0x24 || script[p.help + 0x47] != p.help_id ||
            script[p.help + 0x48] != 0x28 || script[p.help + 0x49] != 0x16 ||
            !offset_is(p.resume, 0x07, p.resume_false_target) ||
            script[p.resume + 5] != 0x27 ||
            !original_call(p.resume + 6) || !original_call(p.resume_false_target))
            return RuntimeTutorialPopupBypassResult::Rejected;

        if (grant_patched && help_patched)
            return RuntimeTutorialPopupBypassResult::AlreadyPatched;
        for (const size_t at : {p.grant, p.help})
        {
            script[at] = 0x06; // EX_Jump; all other bytes/offsets stay unchanged.
            std::memcpy(script + at + 1, &p.resume, sizeof(p.resume));
        }
        return RuntimeTutorialPopupBypassResult::Patched;
    }
}
