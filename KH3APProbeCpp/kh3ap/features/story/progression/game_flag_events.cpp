#include "kh3ap/core/platform/game_build.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/story/progression/game_flag_events.h"
#include <DynamicOutput/DynamicOutput.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
namespace kh3ap { namespace {
std::mutex listeners_mutex;
std::set<VBonusFlagEvents*> boolean_listeners, integer_listeners;
uint64_t boolean_original{}, integer_original{};
std::unique_ptr<PLH::x64Detour> boolean_hook, integer_hook;
void written(bool integer, uint64_t name, bool value) {
    std::lock_guard lock(listeners_mutex);
    for(auto* inbox:integer?integer_listeners:boolean_listeners)inbox->write(name,value);
}
void boolean_write(void* storage,uint64_t name,bool value) {
    reinterpret_cast<void(*)(void*,uint64_t,bool)>(boolean_original)(storage,name,value);
    written(false,name,value);
}
void integer_write(void* storage,uint64_t name,int32_t value) {
    reinterpret_cast<void(*)(void*,uint64_t,int32_t)>(integer_original)(storage,name,value);
    written(true,name,true);
}
void install(bool integer) {
    auto& hook=integer?integer_hook:boolean_hook;
    if(hook)return;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE)throw std::runtime_error("flag_image");
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    const bool egs=kh3ap::native_build::egs.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage);
    const bool steam=kh3ap::native_build::steam.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage);
    if(!egs&&!steam)throw std::runtime_error("flag_unsupported_build");
    const uintptr_t rva=integer?(egs?0x110a130:0xe96740):(egs?0x110a420:0xe95080);
    const std::array<uint8_t,20> bool_bytes{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x18,
        0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xec,static_cast<uint8_t>(egs?0x30:0x20)};
    const std::array<uint8_t,20> int_egs{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,
        0x8b,0x05,0x38,0xb0,0xc1,0x07,0x33,0xff,0x3b,0x05};
    const std::array<uint8_t,20> int_steam{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,
        0x8b,0x05,0x88,0x3c,0x84,0x07,0x33,0xff,0x3b,0x05};
    const auto& bytes=integer?(egs?int_egs:int_steam):bool_bytes;
    DWORD64 image{};auto* unwind=RtlLookupFunctionEntry(base+rva,&image,nullptr);
    if(!unwind||image!=base||unwind->BeginAddress!=rva||std::memcmp(reinterpret_cast<void*>(base+rva),bytes.data(),bytes.size()))
        throw std::runtime_error("flag_setter_signature");
    auto& original=integer?integer_original:boolean_original;
    hook=std::make_unique<PLH::x64Detour>(base+rva,integer?reinterpret_cast<uint64_t>(&integer_write):reinterpret_cast<uint64_t>(&boolean_write),&original);
    if(!hook->hook()||!original){hook.reset();throw std::runtime_error("flag_hook_failed");}
    RC::Output::send<RC::LogLevel::Warning>(L"[KH3APProbeCpp] NativeGameFlagEvents installed kind={} storefront={} rva=0x{:X}\n",
        integer?L"integer":L"boolean",egs?L"egs":L"steam",rva);
}
}
void subscribe_game_flags(VBonusFlagEvents& inbox,bool integer) {
    install(integer);std::lock_guard lock(listeners_mutex);
    (integer?integer_listeners:boolean_listeners).insert(&inbox);
}
void unsubscribe_game_flags(VBonusFlagEvents& inbox) {
    std::lock_guard lock(listeners_mutex);boolean_listeners.erase(&inbox);integer_listeners.erase(&inbox);
}
}
