#include "kh3ap/hooks/frame_wait/frame_wait_stub.h"
#define NOMINMAX
#include <Windows.h>
#include <intrin.h>
#include <cassert>
#include <cstring>
#ifdef KH3AP_TEST_POLYHOOK
#include <polyhook2/Detour/x64Detour.hpp>
#endif

namespace {
    uint8_t enabled{};
    uint64_t trampoline{};
    int calls{};
    double seen{};
    double observed[2]{};
    double inputs[2]{60,40};
    __declspec(noinline) void callback(double deadline) {
        assert((reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()) & 15)==8);
        ++calls; seen=deadline;
    }
    void* executable(const std::vector<uint8_t>& bytes) {
        auto p=VirtualAlloc(nullptr,bytes.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        assert(p); std::memcpy(p,bytes.data(),bytes.size());
        DWORD old{}; assert(VirtualProtect(p,bytes.size(),PAGE_EXECUTE_READ,&old));
        assert(FlushInstructionCache(GetCurrentProcess(),p,bytes.size())); return p;
    }
}
int main() {
    const auto code=kh3ap::build_native_frame_wait_stub(reinterpret_cast<uint64_t>(&enabled),
        reinterpret_cast<uint64_t>(&callback),reinterpret_cast<uint64_t>(&trampoline));
    void* stub=executable(code);
    std::vector<uint8_t> driver{0x53,0x48,0x83,0xEC,0x40, // push rbx; sub rsp,64: aligned native call site
        0xF3,0x44,0x0F,0x7F,0x44,0x24,0x20, // save xmm8
        0xF3,0x44,0x0F,0x7F,0x4C,0x24,0x30, // save xmm9
        0x48,0xB8};
    auto address=[&](uint64_t a){for(unsigned i=0;i<8;++i)driver.push_back(static_cast<uint8_t>(a>>(8*i)));};
    address(reinterpret_cast<uint64_t>(inputs));
    driver.insert(driver.end(),{0xF2,0x44,0x0F,0x10,0x00,0xF2,0x44,0x0F,0x10,0x48,0x08,0x48,0xB8});
    address(reinterpret_cast<uint64_t>(stub));driver.insert(driver.end(),{0xFF,0xE0});
    const size_t continuation=driver.size();
    driver.insert(driver.end(),{0x48,0xB8});address(reinterpret_cast<uint64_t>(observed));
    driver.insert(driver.end(),{0xF2,0x44,0x0F,0x11,0x00,0xF2,0x44,0x0F,0x11,0x48,0x08,
        0xF3,0x44,0x0F,0x6F,0x44,0x24,0x20,0xF3,0x44,0x0F,0x6F,0x4C,0x24,0x30,
        0x48,0x83,0xC4,0x40,0x5B,0xC3});
    void* entry=executable(driver);trampoline=reinterpret_cast<uint64_t>(entry)+continuation;
    auto run=reinterpret_cast<void(*)()>(entry);
    for(int i=0;i<100;++i) {
        enabled=0;run();assert(calls==i);assert(observed[0]==60 && observed[1]==40);
        enabled=1;run();assert(calls==i+1 && seen==20);assert(observed[0]==60 && observed[1]==40);
    }
#ifdef KH3AP_TEST_POLYHOOK
    // Replace the driver's jump with an indirect CALL, matching the game's
    // internal site. This must patch the CALL, never its target function.
    const auto base_driver=driver;
    for(bool prefix_xor : {false,true}) {
    driver=base_driver;
    std::vector<uint8_t> callee_bytes(32,0x90);
    callee_bytes[0]=0xC3;
    void* callee=executable(callee_bytes);
    const size_t site_offset=continuation-12;
    const size_t call_offset=site_offset+(prefix_xor?2:0);
    if(prefix_xor) { driver[site_offset]=0x33; driver[site_offset+1]=0xC9; }
    driver[call_offset]=0xFF; driver[call_offset+1]=0x15;
    const int32_t displacement=static_cast<int32_t>(driver.size()-(call_offset+6));
    std::memcpy(driver.data()+call_offset+2,&displacement,4);
    for(size_t i=call_offset+6;i<continuation;++i) driver[i]=0x90;
    address(reinterpret_cast<uint64_t>(callee));
    void* hooked_entry=executable(driver);
    const auto site=reinterpret_cast<uint64_t>(hooked_entry)+site_offset;
    PLH::x64Detour hook(site,reinterpret_cast<uint64_t>(stub),&trampoline);
    kh3ap::configure_native_frame_wait_detour(hook);
    assert(hook.hook());
    assert(*reinterpret_cast<const uint16_t*>(site)==0x25FF);
    assert(std::memcmp(callee,callee_bytes.data(),callee_bytes.size())==0);
    auto hooked_run=reinterpret_cast<void(*)()>(hooked_entry);
    calls=0;
    for(int i=0;i<100;++i) {
        enabled=0;hooked_run();assert(calls==i);
        enabled=1;hooked_run();assert(calls==i+1 && seen==20);
        assert(observed[0]==60 && observed[1]==40);
    }
    assert(hook.unHook());
    assert(std::memcmp(reinterpret_cast<void*>(site),driver.data()+site_offset,6)==0);
    VirtualFree(hooked_entry,0,MEM_RELEASE);VirtualFree(callee,0,MEM_RELEASE);
    }
#endif
    VirtualFree(entry,0,MEM_RELEASE);VirtualFree(stub,0,MEM_RELEASE);
}
