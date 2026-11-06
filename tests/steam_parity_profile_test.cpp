#define NOMINMAX
#include <Windows.h>
#include "kh3ap/core/platform/steam_parity_profile.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace kh3ap;
int main(int argc,char** argv) {
    assert(!steam_parity_native_profile(0,0));
    assert(!steam_parity_native_profile(0x66743f34,0xaebf000));
    assert(!steam_parity_native_profile(0x60547d11,0xa969000));
    assert(steam_parity_native_profile(0x60547d11,0xaebf000));
    assert(steam_parity_native_profile(0x66743f34,0xa969000));
    for(int arg=1;arg<argc;++arg) {
        std::ifstream file(argv[arg],std::ios::binary);assert(file);
        std::vector<uint8_t> disk{std::istreambuf_iterator<char>{file},{}};
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
        const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(disk.data()+dos->e_lfanew);
        const auto* profile=steam_parity_native_profile(pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage);assert(profile);
        std::vector<uint8_t> image(pe->OptionalHeader.SizeOfImage);
        const auto* sections=IMAGE_FIRST_SECTION(pe);
        for(unsigned i=0;i<pe->FileHeader.NumberOfSections;++i) {
            const auto& section=sections[i];assert(section.PointerToRawData+section.SizeOfRawData<=disk.size());
            assert(section.VirtualAddress+section.SizeOfRawData<=image.size());
            std::memcpy(image.data()+section.VirtualAddress,disk.data()+section.PointerToRawData,section.SizeOfRawData);
        }
        for(const auto& site:{profile->adversary_writer,profile->adversary_store,profile->save_accessor,
                profile->counter_layout,profile->cuisine_query,profile->minimap_select,profile->minimap_compare}) {
            assert(site.rva+site.bytes.size()<image.size());
            assert(!std::memcmp(image.data()+site.rva,site.bytes.data(),site.bytes.size()));
        }
        const auto& directory=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        const auto* entries=reinterpret_cast<const RUNTIME_FUNCTION*>(image.data()+directory.VirtualAddress);
        bool found=false;
        for(unsigned i=0;i<directory.Size/sizeof(RUNTIME_FUNCTION);++i)
            if(entries[i].BeginAddress==profile->minimap_select.rva) {
                assert(entries[i].EndAddress==profile->minimap_end);found=true;
            }
        assert(found);
    }
}
