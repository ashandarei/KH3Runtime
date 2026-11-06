#include "kh3ap/features/world/save_destination.h"
#include <cassert>
#include <cstring>
#include <vector>

int main()
{
    using namespace kh3ap;
    using namespace std::literals;
    std::vector<std::byte> catalog(native_catalog_entry_stride * 2);
    const uintptr_t base = 0x10000;
    auto read = [&](uintptr_t address, auto& value)
    {
        if (address < base || address - base + sizeof(value) > catalog.size())
        {
            return false;
        }
        std::memcpy(&value, catalog.data() + address - base, sizeof(value));
        return true;
    };
    auto write = [&](size_t entry, uint8_t slot, const char* map)
    {
        auto* data = catalog.data() + entry * native_catalog_entry_stride;
        std::memcpy(data + native_catalog_slot_offset, &slot, sizeof(slot));
        std::memset(data + native_catalog_map_offset, 0, native_catalog_map_capacity);
        std::memcpy(data + native_catalog_map_offset, map, std::strlen(map));
    };
    write(0, 0, "/Game/Maps/GardenOfAssemblage/umap/GoA_01");
    write(1, 50, "/Game/Maps/APKH02Mirrors/APKH02Depths");
    auto destination = native_save_destination(base, 2, 0, read);
    assert(destination && !kh02_map_package(std::string_view(*destination)));
    destination = native_save_destination(base, 2, 50, read);
    assert(destination && kh02_map_package(std::string_view(*destination)));
    assert(kh02_map_package(L"/Game/Maps/APKH02CastleTown/APKH02CastleTown"sv));
    assert(kh02_map_package("/Game/Maps/APKH02WorldWithin/APKH02WorldWithin"sv));
    assert(!kh02_map_package("/Game/Maps/APKH02MirrorsWrong/Map"sv));
    assert(!kh02_map_package("/Game/Maps/APKH02Mirrors/"sv));
    write(0, 0, "/Game/Levels/ts/ts_02/ts_02");
    assert(native_save_destination(base, 2, 0, read));
    assert(!native_save_destination(base, 2, 99, read));
    assert(!native_save_destination(base, 101, 0, read));
    assert(!native_save_destination(base, 2, -1, read));
    assert(!native_save_destination(0, 2, 0, read));
    assert(!native_save_destination(base, 3, 99, read));
    for (const char* malformed : {"", "/Other/Map", "/Game/", "/Game/Map/", "/Game//Map", "/Game/../Map"})
    {
        write(0, 0, malformed);
        assert(!native_save_destination(base, 2, 0, read));
    }
    std::memset(catalog.data() + native_catalog_map_offset, 'a', native_catalog_map_capacity);
    assert(!native_save_destination(base, 2, 0, read));
}
