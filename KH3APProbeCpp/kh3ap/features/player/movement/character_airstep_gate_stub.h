#pragma once
#include <cstdint>
#include <vector>

namespace kh3ap::character_airstep
{
    // At the audited internal compare, RSP is aligned and AL contains the
    // native character kind. Both target-discovery helpers keep the pawn in RBX.
    // Change only that comparison's input, never the pawn's actual identity.
    inline auto build_aim_kind_gate_stub(uint64_t callback, uint64_t trampoline)
        -> std::vector<uint8_t>
    {
        std::vector<uint8_t> bytes{
            0x9c, 0x50, 0x51, 0x52,                         // flags, rax, rcx, rdx
            0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, // r8-r11
            0x48, 0x81, 0xec, 0x80, 0, 0, 0};              // shadow + xmm0-xmm5
        const auto address = [&](uint64_t value)
        {
            for (unsigned shift = 0; shift < 64; shift += 8)
            {
                bytes.push_back(static_cast<uint8_t>(value >> shift));
            }
        };
        const auto xmm = [&](uint8_t opcode)
        {
            for (uint8_t index = 0; index < 6; ++index)
            {
                bytes.insert(bytes.end(), {0xf3, 0x0f, opcode,
                    static_cast<uint8_t>(0x44 + 8 * index), 0x24,
                    static_cast<uint8_t>(0x20 + 16 * index)});
            }
        };
        xmm(0x7f); // movdqu [rsp+offset], xmmN
        bytes.insert(bytes.end(), {0x48, 0x89, 0xd9,
            0x0f, 0xb6, 0xd0, 0x48, 0xb8}); // rcx=pawn; edx=native AL; rax=callback
        address(callback);
        bytes.insert(bytes.end(), {0xff, 0xd0,
            0x88, 0x84, 0x24, 0xb0, 0, 0, 0}); // replace only saved AL
        xmm(0x6f); // movdqu xmmN, [rsp+offset]
        bytes.insert(bytes.end(), {0x48, 0x81, 0xc4, 0x80, 0, 0, 0,
            0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58,
            0x5a, 0x59, 0x58, 0x9d,
            0xff, 0x25, 0, 0, 0, 0}); // original cmp/jne via the trampoline
        address(trampoline);
        return bytes;
    }

    template<typename Detour>
    void configure_aim_kind_gate_detour(Detour& hook)
    {
        hook.setIsFollowCallOnFnAddress(false);
        // The cmp/jne block is eight bytes. Never use a register-spoiling or
        // fourteen-byte jump that consumes the following native branch.
        hook.setDetourScheme(Detour::VALLOC2);
    }
}
