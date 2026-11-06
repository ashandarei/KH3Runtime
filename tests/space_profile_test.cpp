#include "kh3ap/features/world/space/space_profile.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
using namespace kh3ap::space;

// Read executable sections as data only; never load or execute the game.
void verify_image(const char* path) {
    std::ifstream file(path,std::ios::binary);
    assert(file);
    std::vector<uint8_t> disk{std::istreambuf_iterator<char>(file),{}};
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
    assert(dos->e_magic==IMAGE_DOS_SIGNATURE);
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS64*>(disk.data()+dos->e_lfanew);
    assert(pe->Signature==IMAGE_NT_SIGNATURE);
    auto profile=select_native_profile(pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage);
    assert(profile.supported);
    std::vector<uint8_t> image(pe->OptionalHeader.SizeOfImage);
    const auto* sections=IMAGE_FIRST_SECTION(pe);
    for (unsigned i=0;i<pe->FileHeader.NumberOfSections;++i) {
        const auto& section=sections[i];
        assert(section.PointerToRawData+section.SizeOfRawData<=disk.size());
        assert(section.VirtualAddress+section.SizeOfRawData<=image.size());
        std::memcpy(image.data()+section.VirtualAddress,disk.data()+section.PointerToRawData,section.SizeOfRawData);
    }
    auto check=[&](uintptr_t rva,const auto& signature) {
        assert(rva+signature.size()<=image.size());
        assert(!std::memcmp(image.data()+rva,signature.data(),signature.size()));
    };
    check(profile.flight_input_rva,profile.flight_input_bytes);
    assert(*reinterpret_cast<const uint64_t*>(image.data()+profile.flight_controller_vtable+0xd38)==pe->OptionalHeader.ImageBase+profile.flight_input_rva);
    check(profile.overview_default_rva,profile.overview_default_bytes);
    check(profile.first_embark_rva,profile.first_embark_bytes);
    check(profile.save_menu_rebuild_rva,profile.save_menu_rebuild_bytes);
    check(profile.landing_list_rva,profile.landing_list_bytes);
    check(profile.mission_list_rva,profile.mission_list_bytes);
    check(profile.text_lookup_rva,profile.text_lookup_bytes);
    check(profile.text_format_number_rva,profile.text_format_number_bytes);
    check(profile.string_assign_rva,profile.string_assign_bytes);
    check(profile.message_input_rva,profile.message_input_bytes);
    assert(*reinterpret_cast<const uint64_t*>(image.data()+profile.message_vtable+89*8)==pe->OptionalHeader.ImageBase+profile.message_input_rva);
    check(profile.embark_help_rva,profile.embark_help_bytes);
    check(profile.embark_focus_help_rva,profile.embark_focus_help_bytes);
    check(profile.embark_selector_rva,profile.embark_selector_bytes);
    check(profile.embark_input_rva,profile.embark_input_bytes);
    check(profile.save_rva,profile.save_bytes);
    check(profile.jump_rva,profile.jump_bytes);
    check(profile.landing_rva,profile.landing_bytes);
    check(profile.lock_rva,profile.lock_bytes);
    check(profile.mission_rva,profile.mission_bytes);
    check(profile.overview_rva,profile.overview_bytes);
    check(profile.icon_state_rva,profile.icon_state_bytes);
    check(profile.travel_begin_rva,profile.travel_begin_bytes);
    check(profile.travel_query_rva,profile.travel_query_bytes);
    check(profile.refresh_overview_rva,profile.refresh_overview_bytes);
    check(profile.battle_end_rva,profile.battle_end_bytes);
    check(profile.weak_assign_rva,profile.weak_assign_bytes);
    if(profile.sphere_open_rva)check(profile.sphere_open_rva,profile.sphere_open_bytes);
    for(const auto& signature:profile.notification_signatures)check(signature.rva,signature.bytes);
    if(profile.steam) {
        // SaveMenuDataAsset's real PostLoad slot; catches a similar-prologue
        // function that cannot rebuild this asset's row cache.
        assert(*reinterpret_cast<const uint64_t*>(image.data()+0x6a4dba0)==pe->OptionalHeader.ImageBase+profile.save_menu_rebuild_rva);
        assert(*reinterpret_cast<const uint32_t*>(image.data()+0x17dd01c)==0x17dbb1e);
    }
    std::cout << "Verified native signatures: " << path << '\n';
}
int main(int argc,char** argv) {
    assert(select_native_profile(0x60547D11,0xAEBF000).supported);
    assert(select_native_profile(0x66743F34,0xA969000).steam);
    assert(!select_native_profile(0x66743F34,0xAEBF000).supported);
    assert(!select_native_profile(0x60547D11,0xA969000).supported);
    assert(!select_native_profile(0,0).supported);
    assert(select_native_profile(0x60547D11,0xAEBF000).sphere_open_flags==0x8c2da);
    assert(select_native_profile(0x66743F34,0xA969000).sphere_open_rva==0x1809970);
    assert(select_native_profile(0x66743F34,0xA969000).sphere_open_flags==0x8c2da);
    assert(select_native_profile(0x60547D11,0xAEBF000).text_formatter_offset==0x608);
    assert(select_native_profile(0x66743F34,0xA969000).text_formatter_offset==0x670);
    assert(select_native_profile(0x60547D11,0xAEBF000).mission_menu_state==37);
    assert(select_native_profile(0x66743F34,0xA969000).mission_menu_state==37);
    for(int i=1;i<argc;++i)verify_image(argv[i]);
}
