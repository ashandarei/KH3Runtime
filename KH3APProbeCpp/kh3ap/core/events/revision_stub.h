#pragma once
#include <cstdint>
#include <vector>
namespace kh3ap {
inline auto build_native_revision_stub(uint64_t revision,uint64_t trampoline) -> std::vector<uint8_t> {
    std::vector<uint8_t> code{0x9c,0x50,0x48,0xb8};
    const auto append=[&](uint64_t value){for(int i=0;i<8;++i)code.push_back(uint8_t(value>>(i*8)));};
    append(revision);
    code.insert(code.end(),{0xf0,0x48,0xff,0x00,0x58,0x9d,0xff,0x25,0,0,0,0});
    append(trampoline);
    return code;
}
}
