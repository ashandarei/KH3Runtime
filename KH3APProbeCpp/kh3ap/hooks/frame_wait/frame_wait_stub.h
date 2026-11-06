#pragma once
#include <cstdint>
#include <vector>

namespace kh3ap
{
    template<typename Detour>
    auto configure_native_frame_wait_detour(Detour& hook) -> void
    {
        hook.setIsFollowCallOnFnAddress(false);
        hook.setDetourScheme(Detour::VALLOC2);
    }

    inline auto build_native_frame_wait_stub(uint64_t enabled, uint64_t callback,
                                             uint64_t trampoline_field) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> bytes;
        auto address = [&](uint64_t value) {
            for (unsigned i=0;i<8;++i) bytes.push_back(static_cast<uint8_t>(value>>(i*8)));
        };
        bytes.insert(bytes.end(),{0x48,0xB8}); address(enabled);
        bytes.insert(bytes.end(),{0x80,0x38,0,0x74,0}); // cmp [rax],0; je trampoline
        const auto branch_end=bytes.size();
        bytes.insert(bytes.end(),{0x48,0x83,0xEC,0x20, // aligned Win64 shadow space
            0x66,0x41,0x0F,0x28,0xC0, // movapd xmm0,xmm8
            0xF2,0x41,0x0F,0x5C,0xC1, // subsd xmm0,xmm9
            0x48,0xB8}); address(callback);
        bytes.insert(bytes.end(),{0xFF,0xD0,0x48,0x83,0xC4,0x20});
        bytes[branch_end-1]=static_cast<uint8_t>(bytes.size()-branch_end);
        bytes.insert(bytes.end(),{0x48,0xB8}); address(trampoline_field);
        bytes.insert(bytes.end(),{0xFF,0x20}); // jmp qword ptr [rax]
        return bytes;
    }
}
