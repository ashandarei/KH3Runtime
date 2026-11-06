#include "kh3ap/core/events/revision_stub.h"
#include <atomic>
#include <cassert>
#include <cstring>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
int main() {
    std::atomic<uint64_t> revision{};
    const auto bytes=kh3ap::build_native_revision_stub(reinterpret_cast<uint64_t>(&revision),123);
    assert(bytes.size()==32&&bytes[0]==0x9c&&bytes[1]==0x50&&bytes[16]==0x58&&bytes[17]==0x9d);
#ifdef _WIN32
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    assert(memory);memory[128]=0xc3;
    const auto stub=kh3ap::build_native_revision_stub(reinterpret_cast<uint64_t>(&revision),reinterpret_cast<uint64_t>(memory+128));
    std::memcpy(memory+64,stub.data(),stub.size());
    // Native caller sets RAX and CF, calls the real generated stub, then records
    // flags immediately after the continuation returns. The stub must change
    // only its atomic revision and preserve the original instruction inputs.
    std::vector<uint8_t> caller{0x48,0x83,0xec,0x28,0x49,0xba};
    const auto append=[&](uint64_t n){for(int i=0;i<8;++i)caller.push_back(uint8_t(n>>(8*i)));};
    append(reinterpret_cast<uint64_t>(memory+64));caller.insert(caller.end(),{0x48,0xb8});
    constexpr uint64_t original=0x123456789abcdef0;append(original);
    const auto carry_offset=caller.size();caller.push_back(0xf9);
    caller.insert(caller.end(),{0x41,0xff,0xd2,0x9c,0x5a,0x48,0x89,0x11,0x48,0x83,0xc4,0x28,0xc3});
    for(int carry=0;carry<2;++carry) {
        caller[carry_offset]=carry?0xf9:0xf8;std::memcpy(memory,caller.data(),caller.size());
        FlushInstructionCache(GetCurrentProcess(),memory,4096);
        uint64_t flags{};auto result=reinterpret_cast<uint64_t(*)(uint64_t*)>(memory)(&flags);
        assert(result==original&&int(flags&1)==carry);
    }
    assert(revision.load()==2);VirtualFree(memory,0,MEM_RELEASE);
#endif
}
