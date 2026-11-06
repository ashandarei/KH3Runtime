#include "kh3ap/features/tutorials/link_tutorial_bypass.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>

using namespace kh3ap;

static void instruction(std::vector<uint8_t>& code, size_t at, uint8_t op, uint32_t value)
{
    code[at] = op;
    std::memcpy(code.data() + at + 1, &value, sizeof(value));
}

static auto fixture(const RuntimeLinkTutorialProfile& p) -> std::vector<uint8_t>
{
    std::vector<uint8_t> code(p.script_size, 0xCC);
    for (auto at : {p.grant, p.help, size_t(p.resume + 6), size_t(p.resume_false_target)})
    {
        code[at] = 0x19;
        code[at + 1] = 0x20;
    }
    code[p.conversion_call] = code[p.grant + 0x16] = code[p.help + 0x16] = 0x1C;
    code[p.item_id] = 0x24;
    code[p.item_id + 1] = p.item;
    instruction(code, p.grant + 0x2D, 0x5B, p.grant_resume);
    instruction(code, p.help + 0x2D, 0x5B, p.resume);
    code[p.help + 0x46] = 0x24;
    code[p.help + 0x47] = p.help_id;
    code[p.help + 0x48] = 0x28;
    code[p.help + 0x49] = 0x16;
    instruction(code, p.resume, 0x07, p.resume_false_target);
    code[p.resume + 5] = 0x27;
    return code;
}

int main()
{
    // The existing popup-only patch may run first. Both patches must use
    // the same 8-Bit Blast continuation so a prepatched help still validates.
    constexpr auto pixel = runtime_link_tutorial_profiles[static_cast<size_t>(RuntimeLinkTutorialVariant::Pixel)];
    static_assert(pixel.item == 7);
    static_assert(pixel.script_size == runtime_toy_box_eight_bit_blast_script_size);
    static_assert(pixel.help == runtime_toy_box_eight_bit_blast_callsite_offset);
    static_assert(pixel.resume == runtime_toy_box_eight_bit_blast_latent_resume_offset);
    static_assert(pixel.resume_false_target == runtime_toy_box_eight_bit_blast_latent_resume_jump_target);
    for (size_t index = 0; index < runtime_link_tutorial_count; ++index)
    {
        const auto variant = static_cast<RuntimeLinkTutorialVariant>(index);
        const auto& p = runtime_link_tutorial_profiles[index];
        const auto original = fixture(p);
        auto code = original;
        assert(try_patch_runtime_link_tutorial_bytecode(code.data(), code.size(), variant) ==
               RuntimeTutorialPopupBypassResult::Patched);
        auto expected = original;
        instruction(expected, p.grant, 0x06, p.resume);
        instruction(expected, p.help, 0x06, p.resume);
        // Only these two local callsites change. Story/check flags, all other
        // item grants, latent targets and cleanup bytes remain untouched.
        assert(code == expected);
        for (auto entry : {p.grant, p.help})
        {
            uint32_t next{};
            std::memcpy(&next, code.data() + entry + 1, sizeof(next));
            assert(code[entry] == 0x06 && next == p.resume);
            assert(code[next] == 0x07 && code[next + 5] == 0x27);
            assert(code[next + 6] == 0x19); // Existing cleanup continuation.
        }
        assert(try_patch_runtime_link_tutorial_bytecode(code.data(), code.size(), variant) ==
               RuntimeTutorialPopupBypassResult::AlreadyPatched);
        assert(code == expected);

        // A reloaded class has fresh code and must be patched again.
        code = original;
        assert(try_patch_runtime_link_tutorial_bytecode(code.data(), code.size(), variant) ==
               RuntimeTutorialPopupBypassResult::Patched);

        // A damaged anchor rejects atomically, never removing just the popup
        // while leaving an unvalidated grant active (or vice versa).
        for (size_t at : {p.grant, p.grant + 1, p.grant + 0x16, p.grant + 0x2E,
                          p.conversion_call, p.item_id, p.item_id + 1,
                          p.help, p.help + 1, p.help + 0x16, p.help + 0x2E,
                          p.help + 0x46, p.help + 0x47, p.help + 0x48, p.help + 0x49,
                          size_t(p.resume), size_t(p.resume + 1), size_t(p.resume + 5),
                          size_t(p.resume + 6), size_t(p.resume_false_target)})
        {
            code = original;
            code[at] ^= 0xFF;
            const auto before = code;
            assert(try_patch_runtime_link_tutorial_bytecode(code.data(), code.size(), variant) ==
                   RuntimeTutorialPopupBypassResult::Rejected);
            assert(code == before);
        }
        for (auto entry : {p.grant, p.help})
        {
            code = original;
            instruction(code, entry, 0x06, p.resume);
            assert(try_patch_runtime_link_tutorial_bytecode(code.data(), code.size(), variant) ==
                   RuntimeTutorialPopupBypassResult::Patched);
            assert(code == expected);
        }
        code = original;
        assert(try_patch_runtime_link_tutorial_bytecode(code.data(), code.size() - 1, variant) ==
               RuntimeTutorialPopupBypassResult::Rejected);
        assert(try_patch_runtime_link_tutorial_bytecode(nullptr, code.size(), variant) ==
               RuntimeTutorialPopupBypassResult::Rejected);
        assert(try_patch_runtime_link_tutorial_bytecode(code.data(), code.size(), RuntimeLinkTutorialVariant::Count) ==
               RuntimeTutorialPopupBypassResult::Rejected);
        assert(code == original);
    }
}
